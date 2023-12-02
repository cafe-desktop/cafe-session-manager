/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <ctk/ctk.h> /* for logout dialog */
#include <gio/gio.h> /* for gsettings */
#include <cdk/cdkx.h>

#include "csm-manager.h"
#include "csm-manager-glue.h"

#include "csm-store.h"
#include "csm-inhibitor.h"
#include "csm-presence.h"

#include "csm-xsmp-client.h"
#include "csm-dbus-client.h"

#include "csm-autostart-app.h"

#include "csm-util.h"
#include "cdm.h"
#include "csm-logout-dialog.h"
#include "csm-inhibit-dialog.h"
#include "csm-consolekit.h"
#ifdef HAVE_SYSTEMD
#include "csm-systemd.h"
#endif
#include "csm-session-save.h"

#define CSM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_MANAGER, CsmManagerPrivate))

#define CSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define CSM_MANAGER_DBUS_NAME "org.cafe.SessionManager"

#define CSM_MANAGER_PHASE_TIMEOUT 30 /* seconds */

/* In the exit phase, all apps were already given the chance to inhibit the session end
 * At that stage we don't want to wait much for apps to respond, we want to exit, and fast.
 */
#define CSM_MANAGER_EXIT_PHASE_TIMEOUT 1 /* seconds */

#define CDM_FLEXISERVER_COMMAND "cdmflexiserver"
#define CDM_FLEXISERVER_ARGS    "--startnew Standard"

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"


#define LOCKDOWN_SCHEMA              "org.cafe.lockdown"
#define KEY_LOCK_DISABLE             "disable-lock-screen"
#define KEY_LOG_OUT_DISABLE          "disable-log-out"
#define KEY_USER_SWITCH_DISABLE      "disable-user-switching"

#define SESSION_SCHEMA               "org.cafe.session"
#define KEY_IDLE_DELAY               "idle-delay"
#define KEY_AUTOSAVE                 "auto-save-session"

#define SCREENSAVER_SCHEMA           "org.cafe.screensaver"
#define KEY_SLEEP_LOCK               "lock-enabled"

#ifdef __GNUC__
#define UNUSED_VARIABLE __attribute__ ((unused))
#else
#define UNUSED_VARIABLE
#endif

typedef enum
{
        CSM_MANAGER_LOGOUT_NONE,
        CSM_MANAGER_LOGOUT_LOGOUT,
        CSM_MANAGER_LOGOUT_REBOOT,
        CSM_MANAGER_LOGOUT_REBOOT_INTERACT,
        CSM_MANAGER_LOGOUT_REBOOT_CDM,
        CSM_MANAGER_LOGOUT_SHUTDOWN,
        CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT,
        CSM_MANAGER_LOGOUT_SHUTDOWN_CDM
} CsmManagerLogoutType;

typedef struct {
        gboolean                failsafe;
        CsmStore               *clients;
        CsmStore               *inhibitors;
        CsmStore               *apps;
        CsmPresence            *presence;

        /* Current status */
        CsmManagerPhase         phase;
        guint                   phase_timeout_id;
        GSList                 *pending_apps;
        CsmManagerLogoutMode    logout_mode;
        GSList                 *query_clients;
        guint                   query_timeout_id;
        /* This is used for CSM_MANAGER_PHASE_END_SESSION only at the moment,
         * since it uses a sublist of all running client that replied in a
         * specific way */
        GSList                 *next_query_clients;
        /* This is the action that will be done just before we exit */
        CsmManagerLogoutType    logout_type;

        CtkWidget              *inhibit_dialog;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        GSettings              *settings_session;
        GSettings              *settings_lockdown;
        GSettings              *settings_screensaver;

        const char             *renderer;

        DBusGProxy             *bus_proxy;
        DBusGConnection        *connection;
        gboolean                dbus_disconnected : 1;
} CsmManagerPrivate;

enum {
        PROP_0,
        PROP_CLIENT_STORE,
        PROP_RENDERER,
        PROP_FAILSAFE
};

enum {
        PHASE_CHANGED,
        CLIENT_ADDED,
        CLIENT_REMOVED,
        INHIBITOR_ADDED,
        INHIBITOR_REMOVED,
        SESSION_RUNNING,
        SESSION_OVER,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void     csm_manager_finalize    (GObject         *object);

static gboolean _log_out_is_locked_down     (CsmManager *manager);
static gboolean _switch_user_is_locked_down (CsmManager *manager);

static void     _handle_client_end_session_response (CsmManager *manager,
                                                     CsmClient  *client,
                                                     gboolean    is_ok,
                                                     gboolean    do_last,
                                                     gboolean    cancel,
                                                     const char *reason);

static gboolean auto_save_is_enabled (CsmManager *manager);
static void     maybe_save_session   (CsmManager *manager);

static gpointer manager_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CsmManager, csm_manager, G_TYPE_OBJECT)

GQuark
csm_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("csm_manager_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
csm_manager_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (CSM_MANAGER_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION, "NotInInitialization"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_NOT_IN_RUNNING, "NotInRunning"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_ALREADY_REGISTERED, "AlreadyRegistered"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_NOT_REGISTERED, "NotRegistered"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_INVALID_OPTION, "InvalidOption"),
                        ENUM_ENTRY (CSM_MANAGER_ERROR_LOCKED_DOWN, "LockedDown"),
                        { 0, 0, 0 }
                };

                g_assert (CSM_MANAGER_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("CsmManagerError", values);
        }

        return etype;
}

static gboolean
_debug_client (const char *id,
               CsmClient  *client,
               CsmManager *manager)
{
        g_debug ("CsmManager: Client %s", csm_client_peek_id (client));
        return FALSE;
}

static void
debug_clients (CsmManager *manager)
{
        CsmManagerPrivate *priv;
        priv = csm_manager_get_instance_private (manager);
        csm_store_foreach (priv->clients,
                           (CsmStoreFunc)_debug_client,
                           manager);
}

static gboolean
_debug_inhibitor (const char    *id,
                  CsmInhibitor  *inhibitor,
                  CsmManager    *manager)
{
        g_debug ("CsmManager: Inhibitor app:%s client:%s bus-name:%s reason:%s",
                 csm_inhibitor_peek_app_id (inhibitor),
                 csm_inhibitor_peek_client_id (inhibitor),
                 csm_inhibitor_peek_bus_name (inhibitor),
                 csm_inhibitor_peek_reason (inhibitor));
        return FALSE;
}

static void
debug_inhibitors (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        csm_store_foreach (priv->inhibitors,
                           (CsmStoreFunc)_debug_inhibitor,
                           manager);
}

static gboolean
_find_by_cookie (const char   *id,
                 CsmInhibitor *inhibitor,
                 guint        *cookie_ap)
{
        guint cookie_b;

        cookie_b = csm_inhibitor_peek_cookie (inhibitor);

        return (*cookie_ap == cookie_b);
}

static gboolean
_find_by_startup_id (const char *id,
                     CsmClient  *client,
                     const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = csm_client_peek_startup_id (client);
        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static void
app_condition_changed (CsmApp     *app,
                       gboolean    condition,
                       CsmManager *manager)
{
        CsmClient *client;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        g_debug ("CsmManager: app:%s condition changed condition:%d",
                 csm_app_peek_id (app),
                 condition);

        client = (CsmClient *)csm_store_find (priv->clients,
                                              (CsmStoreFunc)_find_by_startup_id,
                                              (char *)csm_app_peek_startup_id (app));

        if (condition) {
                if (!csm_app_is_running (app) && client == NULL) {
                        GError  *error = NULL;
                        gboolean UNUSED_VARIABLE res;

                        g_debug ("CsmManager: starting app '%s'", csm_app_peek_id (app));

                        res = csm_app_start (app, &error);
                        if (error != NULL) {
                                g_warning ("Not able to start app from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                } else {
                        g_debug ("CsmManager: not starting - app still running '%s'", csm_app_peek_id (app));
                }
        } else {
                GError  *error;
                gboolean UNUSED_VARIABLE res;

                if (client != NULL) {
                        /* Kill client in case condition if false and make sure it won't
                         * be automatically restarted by adding the client to
                         * condition_clients */
                        priv->condition_clients =
                                g_slist_prepend (priv->condition_clients, client);

                        g_debug ("CsmManager: stopping client %s for app", csm_client_peek_id (client));

                        error = NULL;
                        res = csm_client_stop (client, &error);
                        if (error != NULL) {
                                g_warning ("Not able to stop app client from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                } else {
                        g_debug ("CsmManager: stopping app %s", csm_app_peek_id (app));

                        /* If we don't have a client then we should try to kill the app,
                         * if it is running */
                        error = NULL;
                        if (csm_app_is_running (app)) {
                                res = csm_app_stop (app, &error);
                                if (error != NULL) {
                                       g_warning ("Not able to stop app from its condition: %s",
                                                error->message);
                                   g_error_free (error);
                                }
                        }
                }
        }
}

static const char *
phase_num_to_name (guint phase)
{
        const char *name;

        switch (phase) {
        case CSM_MANAGER_PHASE_STARTUP:
                name = "STARTUP";
                break;
        case CSM_MANAGER_PHASE_INITIALIZATION:
                name = "INITIALIZATION";
                break;
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
                name = "WINDOW_MANAGER";
                break;
        case CSM_MANAGER_PHASE_PANEL:
                name = "PANEL";
                break;
        case CSM_MANAGER_PHASE_DESKTOP:
                name = "DESKTOP";
                break;
        case CSM_MANAGER_PHASE_APPLICATION:
                name = "APPLICATION";
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                name = "RUNNING";
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                name = "QUERY_END_SESSION";
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                name = "END_SESSION";
                break;
        case CSM_MANAGER_PHASE_EXIT:
                name = "EXIT";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return name;
}

static void start_phase (CsmManager *manager);

static void
quit_request_completed_consolekit (CsmConsolekit *consolekit,
                                   GError        *error,
                                   gpointer       user_data)
{
        CdmLogoutAction fallback_action = GPOINTER_TO_INT (user_data);

        if (error != NULL) {
                cdm_set_logout_action (fallback_action);
        }

        g_object_unref (consolekit);

        ctk_main_quit ();
}

#ifdef HAVE_SYSTEMD
static void
quit_request_completed_systemd (CsmSystemd *systemd,
                                GError     *error,
                                gpointer    user_data)
{
        CdmLogoutAction fallback_action = GPOINTER_TO_INT (user_data);

        if (error != NULL) {
                cdm_set_logout_action (fallback_action);
        }

        g_object_unref (systemd);

        ctk_main_quit ();
}
#endif

static void
csm_manager_quit (CsmManager *manager)
{
        CsmConsolekit *consolekit;
        CsmManagerPrivate *priv;
#ifdef HAVE_SYSTEMD
        CsmSystemd *systemd;
#endif

        priv = csm_manager_get_instance_private (manager);
        /* See the comment in request_reboot() for some more details about how
         * this works. */

        switch (priv->logout_type) {
        case CSM_MANAGER_LOGOUT_LOGOUT:
                ctk_main_quit ();
                break;
        case CSM_MANAGER_LOGOUT_REBOOT:
        case CSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                cdm_set_logout_action (CDM_LOGOUT_ACTION_NONE);

#ifdef HAVE_SYSTEMD
                if (LOGIND_RUNNING()) {
                    systemd = csm_get_systemd ();
                    g_signal_connect (systemd,
                                      "request-completed",
                                      G_CALLBACK (quit_request_completed_systemd),
                                      GINT_TO_POINTER (CDM_LOGOUT_ACTION_REBOOT));
                    csm_systemd_attempt_restart (systemd);
                }
                else {
#endif
                consolekit = csm_get_consolekit ();
                g_signal_connect (consolekit,
                                  "request-completed",
                                  G_CALLBACK (quit_request_completed_consolekit),
                                  GINT_TO_POINTER (CDM_LOGOUT_ACTION_REBOOT));
                csm_consolekit_attempt_restart (consolekit);
#ifdef HAVE_SYSTEMD
                }
#endif
                break;
        case CSM_MANAGER_LOGOUT_REBOOT_CDM:
                cdm_set_logout_action (CDM_LOGOUT_ACTION_REBOOT);
                ctk_main_quit ();
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
                cdm_set_logout_action (CDM_LOGOUT_ACTION_NONE);

#ifdef HAVE_SYSTEMD
                if (LOGIND_RUNNING()) {
                    systemd = csm_get_systemd ();
                    g_signal_connect (systemd,
                                      "request-completed",
                                      G_CALLBACK (quit_request_completed_systemd),
                                      GINT_TO_POINTER (CDM_LOGOUT_ACTION_SHUTDOWN));
                    csm_systemd_attempt_stop (systemd);
                }
                else {
#endif
                consolekit = csm_get_consolekit ();
                g_signal_connect (consolekit,
                                  "request-completed",
                                  G_CALLBACK (quit_request_completed_consolekit),
                                  GINT_TO_POINTER (CDM_LOGOUT_ACTION_SHUTDOWN));
                csm_consolekit_attempt_stop (consolekit);
#ifdef HAVE_SYSTEMD
                }
#endif
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN_CDM:
                cdm_set_logout_action (CDM_LOGOUT_ACTION_SHUTDOWN);
                ctk_main_quit ();
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
end_phase (CsmManager *manager)
{
        CsmManagerPrivate *priv;
        gboolean start_next_phase = TRUE;

        priv = csm_manager_get_instance_private (manager);

        g_debug ("CsmManager: ending phase %s\n",
                 phase_num_to_name (priv->phase));

        g_slist_free (priv->pending_apps);
        priv->pending_apps = NULL;

        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;

        g_slist_free (priv->next_query_clients);
        priv->next_query_clients = NULL;

        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        switch (priv->phase) {
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                if (_log_out_is_locked_down (manager)) {
                        g_warning ("Unable to logout: Logout has been locked down");
                        start_next_phase = FALSE;
                }
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                if (auto_save_is_enabled (manager))
                        maybe_save_session (manager);
                break;
        case CSM_MANAGER_PHASE_EXIT:
                start_next_phase = FALSE;
                csm_manager_quit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        if (start_next_phase) {
                priv->phase++;
                start_phase (manager);
        }
}

static void
app_registered (CsmApp     *app,
                CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        priv->pending_apps = g_slist_remove (priv->pending_apps, app);
        g_signal_handlers_disconnect_by_func (app, app_registered, manager);

        if (priv->pending_apps == NULL) {
                if (priv->phase_timeout_id > 0) {
                        g_source_remove (priv->phase_timeout_id);
                        priv->phase_timeout_id = 0;
                }

                end_phase (manager);
        }
}

static gboolean
on_phase_timeout (CsmManager *manager)
{
        GSList *a;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        priv->phase_timeout_id = 0;

        switch (priv->phase) {
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                for (a = priv->pending_apps; a; a = a->next) {
                        g_warning ("Application '%s' failed to register before timeout",
                                   csm_app_peek_app_id (a->data));
                        g_signal_handlers_disconnect_by_func (a->data, app_registered, manager);
                        /* FIXME: what if the app was filling in a required slot? */
                }
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
        case CSM_MANAGER_PHASE_END_SESSION:
                break;
        case CSM_MANAGER_PHASE_EXIT:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        end_phase (manager);

        return FALSE;
}

static gboolean
_autostart_delay_timeout (CsmApp *app)
{
        GError *error = NULL;
        gboolean res;

        if (!csm_app_peek_is_disabled (app)
            && !csm_app_peek_is_conditionally_disabled (app)) {
                res = csm_app_start (app, &error);
                if (!res) {
                        if (error != NULL) {
                                g_warning ("Could not launch application '%s': %s",
                                           csm_app_peek_app_id (app),
                                           error->message);
                                g_error_free (error);
                        }
                }
        }

        g_object_unref (app);

        return FALSE;
}

static gboolean
_start_app (const char *id,
            CsmApp     *app,
            CsmManager *manager)
{
        GError  *error;
        gboolean res;
        int      delay;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (csm_app_peek_phase (app) != priv->phase) {
                goto out;
        }

        /* Keep track of app autostart condition in order to react
         * accordingly in the future. */
        g_signal_connect (app,
                          "condition-changed",
                          G_CALLBACK (app_condition_changed),
                          manager);

        if (csm_app_peek_is_disabled (app)
            || csm_app_peek_is_conditionally_disabled (app)) {
                g_debug ("CsmManager: Skipping disabled app: %s", id);
                goto out;
        }

        delay = csm_app_peek_autostart_delay (app);
        if (delay > 0) {
                g_timeout_add_seconds (delay,
                                       (GSourceFunc)_autostart_delay_timeout,
                                       g_object_ref (app));
                g_debug ("CsmManager: %s is scheduled to start in %d seconds", id, delay);
                goto out;
        }

        error = NULL;
        res = csm_app_start (app, &error);
        if (!res) {
                if (error != NULL) {
                        g_warning ("Could not launch application '%s': %s",
                                   csm_app_peek_app_id (app),
                                   error->message);
                        g_error_free (error);
                        error = NULL;
                }
                goto out;
        }

        if (priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                g_signal_connect (app,
                                  "exited",
                                  G_CALLBACK (app_registered),
                                  manager);
                g_signal_connect (app,
                                  "registered",
                                  G_CALLBACK (app_registered),
                                  manager);
                priv->pending_apps = g_slist_prepend (priv->pending_apps, app);
        }
 out:
        return FALSE;
}

static void
do_phase_startup (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        csm_store_foreach (priv->apps,
                           (CsmStoreFunc)_start_app,
                           manager);

        if (priv->pending_apps != NULL) {
                if (priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                        priv->phase_timeout_id = g_timeout_add_seconds (CSM_MANAGER_PHASE_TIMEOUT,
                                                                        (GSourceFunc)on_phase_timeout,
                                                                        manager);
                }
        } else {
                end_phase (manager);
        }
}

typedef struct {
        CsmManager *manager;
        guint       flags;
} ClientEndSessionData;


static gboolean
_client_end_session (CsmClient            *client,
                     ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (data->manager);

        error = NULL;
        ret = csm_client_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: adding client to end-session clients: %s", csm_client_peek_id (client));
                priv->query_clients = g_slist_prepend (priv->query_clients,
                                                       client);
        }

        return FALSE;
}

static gboolean
_client_end_session_helper (const char           *id,
                            CsmClient            *client,
                            ClientEndSessionData *data)
{
        return _client_end_session (client, data);
}

static void
do_phase_end_session (CsmManager *manager)
{
        ClientEndSessionData data;
        CsmManagerPrivate *priv;

        data.manager = manager;
        data.flags = 0;
        priv = csm_manager_get_instance_private (manager);

        if (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (auto_save_is_enabled (manager)) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_SAVE;
        }

        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        if (csm_store_size (priv->clients) > 0) {
                priv->phase_timeout_id = g_timeout_add_seconds (CSM_MANAGER_PHASE_TIMEOUT,
                                                                (GSourceFunc)on_phase_timeout,
                                                                manager);

                csm_store_foreach (priv->clients,
                                   (CsmStoreFunc)_client_end_session_helper,
                                   &data);
        } else {
                end_phase (manager);
        }
}

static void
do_phase_end_session_part_2 (CsmManager *manager)
{
        ClientEndSessionData data;
        CsmManagerPrivate *priv;

        data.manager = manager;
        data.flags = 0;
        priv = csm_manager_get_instance_private (manager);

        if (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (auto_save_is_enabled (manager)) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_SAVE;
        }
        data.flags |= CSM_CLIENT_END_SESSION_FLAG_LAST;

        /* keep the timeout that was started at the beginning of the
         * CSM_MANAGER_PHASE_END_SESSION phase */

        if (g_slist_length (priv->next_query_clients) > 0) {
                g_slist_foreach (priv->next_query_clients,
                                 (GFunc)_client_end_session,
                                 &data);

                g_slist_free (priv->next_query_clients);
                priv->next_query_clients = NULL;
        } else {
                end_phase (manager);
        }
}

static gboolean
_client_stop (const char *id,
              CsmClient  *client,
              gpointer    user_data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = csm_client_stop (client, &error);
        if (! ret) {
                g_warning ("Unable to stop client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: stopped client: %s", csm_client_peek_id (client));
        }

        return FALSE;
}

#ifdef HAVE_SYSTEMD
static void
maybe_restart_user_bus (CsmManager *manager)
{
        CsmSystemd *systemd;
        CsmManagerPrivate *priv;
        GDBusConnection *connection;

        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        priv = csm_manager_get_instance_private (manager);
        if (priv->dbus_disconnected)
                return;

        systemd = csm_get_systemd ();

        if (!csm_systemd_is_last_session_for_user (systemd))
                return;

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_debug ("CsmManager: failed to connect to session bus: %s", error->message);
                return;
        }

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "TryRestartUnit",
                                             g_variant_new ("(ss)", "dbus.service", "replace"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (error != NULL) {
                g_debug ("CsmManager: reloading user bus failed: %s", error->message);
        }
}
#endif

static void
do_phase_exit (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (csm_store_size (priv->clients) > 0) {
                csm_store_foreach (priv->clients,
                                   (CsmStoreFunc)_client_stop,
                                   NULL);
        }

#ifdef HAVE_SYSTEMD
        maybe_restart_user_bus (manager);
#endif

        end_phase (manager);
}

static gboolean
_client_query_end_session (const char           *id,
                           CsmClient            *client,
                           ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (data->manager);

        error = NULL;
        ret = csm_client_query_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: adding client to query clients: %s", csm_client_peek_id (client));
                priv->query_clients = g_slist_prepend (priv->query_clients, client);
        }

        return FALSE;
}

static gboolean
inhibitor_has_flag (gpointer      key,
                    CsmInhibitor *inhibitor,
                    gpointer      data)
{
        guint flag;
        guint flags;

        flag = GPOINTER_TO_UINT (data);

        flags = csm_inhibitor_peek_flags (inhibitor);

        return (flags & flag);
}

static gboolean
csm_manager_is_logout_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_LOGOUT));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
csm_manager_is_idle_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_IDLE));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
_client_cancel_end_session (const char *id,
                            CsmClient  *client,
                            CsmManager *manager)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = csm_client_cancel_end_session (client, &error);
        if (! res) {
                g_warning ("Unable to cancel end session: %s", error->message);
                g_error_free (error);
        }

        return FALSE;
}

static gboolean
inhibitor_is_jit (gpointer      key,
                  CsmInhibitor *inhibitor,
                  CsmManager   *manager)
{
        gboolean    matches;
        const char *id;

        id = csm_inhibitor_peek_client_id (inhibitor);

        matches = (id != NULL && id[0] != '\0');

        return matches;
}

static void
cancel_end_session (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* just ignore if received outside of shutdown */
        if (priv->phase < CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        /* switch back to running phase */
        g_debug ("CsmManager: Cancelling the end of session");

        /* remove the dialog before we remove the inhibitors, else the dialog
         * will activate itself automatically when the last inhibitor will be
         * removed */
        if (priv->inhibit_dialog)
                ctk_widget_destroy (CTK_WIDGET (priv->inhibit_dialog));
        priv->inhibit_dialog = NULL;

        /* clear all JIT inhibitors */
        csm_store_foreach_remove (priv->inhibitors,
                                  (CsmStoreFunc)inhibitor_is_jit,
                                  (gpointer)manager);

        csm_store_foreach (priv->clients,
                           (CsmStoreFunc)_client_cancel_end_session,
                           NULL);

        csm_manager_set_phase (manager, CSM_MANAGER_PHASE_RUNNING);
        priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        priv->logout_type = CSM_MANAGER_LOGOUT_NONE;
        cdm_set_logout_action (CDM_LOGOUT_ACTION_NONE);

        start_phase (manager);
}

static gboolean
process_is_running (const char * name)
{
       int num_processes;
       char * command = g_strdup_printf ("pidof %s | wc -l", name);
       FILE *fp = popen(command, "r");

       if (fscanf(fp, "%d", &num_processes) != 1)
           num_processes = 0;

       pclose(fp);
       g_free (command);

       if (num_processes > 0) {
           return TRUE;
       }
       else {
           return FALSE;
       }
}

static void
manager_switch_user (CsmManager *manager)
{
        GError  *error;
        gboolean res;
        char    *command;
        const gchar *xdg_seat_path = g_getenv ("XDG_SEAT_PATH");

        /* We have to do this here and in request_switch_user() because this
         * function can be called at a later time, not just directly after
         * request_switch_user(). */
        if (_switch_user_is_locked_down (manager)) {
                g_warning ("Unable to switch user: User switching has been locked down");
                return;
        }

        if (process_is_running("cdm")) {
                /* CDM */
                command = g_strdup_printf ("%s %s",
                                           CDM_FLEXISERVER_COMMAND,
                                           CDM_FLEXISERVER_ARGS);

                error = NULL;
                res = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);

                g_free (command);

                if (! res) {
                        g_debug ("CsmManager: Unable to start CDM greeter: %s", error->message);
                        g_error_free (error);
                }
        }
        else if (process_is_running("gdm") || process_is_running("gdm3") || process_is_running("gdm-binary")) {
                /* GDM */
                command = g_strdup_printf ("%s %s",
                                           GDM_FLEXISERVER_COMMAND,
                                           GDM_FLEXISERVER_ARGS);

                error = NULL;
                res = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);

                g_free (command);

                if (! res) {
                        g_debug ("CsmManager: Unable to start GDM greeter: %s", error->message);
                        g_error_free (error);
                }
        }
        else if (xdg_seat_path != NULL) {
                /* LightDM */
                GDBusProxyFlags flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
                GDBusProxy *proxy = NULL;
                error = NULL;

                proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                      flags,
                                                      NULL,
                                                      "org.freedesktop.DisplayManager",
                                                      xdg_seat_path,
                                                      "org.freedesktop.DisplayManager.Seat",
                                                      NULL,
                                                      &error);
                if (proxy != NULL) {
                        g_dbus_proxy_call_sync (proxy,
                                                "SwitchToGreeter",
                                                g_variant_new ("()"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                NULL);
                        g_object_unref (proxy);
                }
                else {
                        g_debug ("CsmManager: Unable to start LightDM greeter: %s", error->message);
                        g_error_free (error);
                }
         }
}

static gboolean
sleep_lock_is_enabled (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (priv->settings_screensaver != NULL)
                return g_settings_get_boolean (priv->settings_screensaver,
                                               KEY_SLEEP_LOCK);
        else
                return FALSE;
}

static void
manager_perhaps_lock (CsmManager *manager)
{
        GError   *error;
        gboolean  ret;

        /* only lock if cafe-screensaver is set to lock */
        if (!sleep_lock_is_enabled (manager)) {
                return;
        }

        /* do this sync to ensure it's on the screen when we start suspending */
        error = NULL;
        ret = g_spawn_command_line_sync ("cafe-screensaver-command --lock", NULL, NULL, NULL, &error);
        if (!ret) {
                g_warning ("Couldn't lock screen: %s", error->message);
                g_error_free (error);
        }
}

static void
manager_attempt_hibernate (CsmManager *manager)
{
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {

                CsmSystemd *systemd;

                systemd = csm_get_systemd ();

                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);

                csm_systemd_attempt_hibernate (systemd);
        }
        else {
#endif
        CsmConsolekit *consolekit;
        consolekit = csm_get_consolekit ();

        gboolean can_hibernate = csm_consolekit_can_hibernate (consolekit);
        if (can_hibernate) {
                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);

                csm_consolekit_attempt_hibernate (consolekit);
        }
#ifdef HAVE_SYSTEMD
        }
#endif
}

static void
manager_attempt_suspend (CsmManager *manager)
{
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {

                CsmSystemd *systemd;

                systemd = csm_get_systemd ();

                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);

                csm_systemd_attempt_suspend (systemd);
        }
        else {
#endif
        CsmConsolekit *consolekit;
        consolekit = csm_get_consolekit ();

        gboolean can_suspend = csm_consolekit_can_suspend (consolekit);
        if (can_suspend) {
                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);

                csm_consolekit_attempt_suspend (consolekit);
        }
#ifdef HAVE_SYSTEMD
        }
#endif
}

static void
do_inhibit_dialog_action (CsmManager *manager,
                          int         action)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        switch (action) {
        case CSM_LOGOUT_ACTION_SWITCH_USER:
                manager_switch_user (manager);
                break;
        case CSM_LOGOUT_ACTION_HIBERNATE:
                manager_attempt_hibernate (manager);
                break;
        case CSM_LOGOUT_ACTION_SLEEP:
                manager_attempt_suspend (manager);
                break;
        case CSM_LOGOUT_ACTION_SHUTDOWN:
        case CSM_LOGOUT_ACTION_REBOOT:
        case CSM_LOGOUT_ACTION_LOGOUT:
                priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_FORCE;
                end_phase (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
inhibit_dialog_response (CsmInhibitDialog *dialog,
                         guint             response_id,
                         CsmManager       *manager)
{
        int action;
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: Inhibit dialog response: %d", response_id);

        priv = csm_manager_get_instance_private (manager);
        /* must destroy dialog before cancelling since we'll
           remove JIT inhibitors and we don't want to trigger
           action. */
        g_object_get (dialog, "action", &action, NULL);
        ctk_widget_destroy (CTK_WIDGET (dialog));
        priv->inhibit_dialog = NULL;

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case CTK_RESPONSE_CANCEL:
        case CTK_RESPONSE_NONE:
        case CTK_RESPONSE_DELETE_EVENT:
                if (action == CSM_LOGOUT_ACTION_LOGOUT
                    || action == CSM_LOGOUT_ACTION_SHUTDOWN
                    || action == CSM_LOGOUT_ACTION_REBOOT) {
                        cancel_end_session (manager);
                }
                break;
        case CTK_RESPONSE_ACCEPT:
                g_debug ("CsmManager: doing action %d", action);
                do_inhibit_dialog_action (manager, action);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
query_end_session_complete (CsmManager *manager)
{
        CsmLogoutAction action;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        g_debug ("CsmManager: query end session complete");

        /* Remove the timeout since this can be called from outside the timer
         * and we don't want to have it called twice */
        if (priv->query_timeout_id > 0) {
                g_source_remove (priv->query_timeout_id);
                priv->query_timeout_id = 0;
        }

        if (! csm_manager_is_logout_inhibited (manager)) {
                end_phase (manager);
                return;
        }

        if (priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                ctk_window_present (CTK_WINDOW (priv->inhibit_dialog));
                return;
        }

        switch (priv->logout_type) {
        case CSM_MANAGER_LOGOUT_LOGOUT:
                action = CSM_LOGOUT_ACTION_LOGOUT;
                break;
        case CSM_MANAGER_LOGOUT_REBOOT:
        case CSM_MANAGER_LOGOUT_REBOOT_INTERACT:
        case CSM_MANAGER_LOGOUT_REBOOT_CDM:
                action = CSM_LOGOUT_ACTION_REBOOT;
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_CDM:
                action = CSM_LOGOUT_ACTION_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating inhibit dialog",
                           priv->logout_type);
                action = CSM_LOGOUT_ACTION_LOGOUT;
                break;
        }

        /* Note: CSM_LOGOUT_ACTION_SHUTDOWN and CSM_LOGOUT_ACTION_REBOOT are
         * actually handled the same way as CSM_LOGOUT_ACTION_LOGOUT in the
         * inhibit dialog; the action, if the button is clicked, will be to
         * simply go to the next phase. */
        priv->inhibit_dialog = csm_inhibit_dialog_new (priv->inhibitors,
                                                       priv->clients,
                                                       action);

        g_signal_connect (priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        ctk_widget_show (priv->inhibit_dialog);

}

static guint32
generate_cookie (void)
{
        guint32 cookie;

        cookie = (guint32)g_random_int_range (1, G_MAXINT32);

        return cookie;
}

static guint32
_generate_unique_cookie (CsmManager *manager)
{
        guint32 cookie;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        do {
                cookie = generate_cookie ();
        } while (csm_store_find (priv->inhibitors, (CsmStoreFunc)_find_by_cookie, &cookie) != NULL);

        return cookie;
}

static gboolean
_on_query_end_session_timeout (CsmManager *manager)
{
        GSList *l;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        priv->query_timeout_id = 0;

        g_debug ("CsmManager: query end session timed out");

        for (l = priv->query_clients; l != NULL; l = l->next) {
                guint         cookie;
                CsmInhibitor *inhibitor;
                const char   *bus_name;
                char         *app_id;

                g_warning ("Client '%s' failed to reply before timeout",
                           csm_client_peek_id (l->data));

                /* Don't add "not responding" inhibitors if logout is forced
                 */
                if (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                        continue;
                }

                /* Add JIT inhibit for unresponsive client */
                if (CSM_IS_DBUS_CLIENT (l->data)) {
                        bus_name = csm_dbus_client_get_bus_name (l->data);
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (csm_client_peek_app_id (l->data));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = csm_client_get_app_name (l->data);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = csm_inhibitor_new_for_client (csm_client_peek_id (l->data),
                                                          app_id,
                                                          CSM_INHIBITOR_FLAG_LOGOUT,
                                                          _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                csm_store_add (priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        }

        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;

        query_end_session_complete (manager);

        return FALSE;
}

static void
do_phase_query_end_session (CsmManager *manager)
{
        ClientEndSessionData data;
        CsmManagerPrivate *priv;

        data.manager = manager;
        data.flags = 0;
        priv = csm_manager_get_instance_private (manager);

        if (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        /* We only query if an app is ready to log out, so we don't use
         * CSM_CLIENT_END_SESSION_FLAG_SAVE here.
         */

        debug_clients (manager);
        g_debug ("CsmManager: sending query-end-session to clients (logout mode: %s)",
                 priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_NORMAL? "normal" :
                 priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE? "forceful":
                 "no confirmation");
        csm_store_foreach (priv->clients,
                           (CsmStoreFunc)_client_query_end_session,
                           &data);

        /* This phase doesn't time out unless logout is forced. Typically, this
         * separate timer is only used to show UI. */
        priv->query_timeout_id = g_timeout_add_seconds (1, (GSourceFunc)_on_query_end_session_timeout, manager);
}

static void
update_idle (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (csm_manager_is_idle_inhibited (manager)) {
                csm_presence_set_idle_enabled (priv->presence, FALSE);
        } else {
                csm_presence_set_idle_enabled (priv->presence, TRUE);
        }
}

static void
start_phase (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        g_debug ("CsmManager: starting phase %s\n",
                 phase_num_to_name (priv->phase));

        /* reset state */
        g_slist_free (priv->pending_apps);
        priv->pending_apps = NULL;
        g_slist_free (priv->query_clients);
        priv->query_clients = NULL;
        g_slist_free (priv->next_query_clients);
        priv->next_query_clients = NULL;

        if (priv->query_timeout_id > 0) {
                g_source_remove (priv->query_timeout_id);
                priv->query_timeout_id = 0;
        }
        if (priv->phase_timeout_id > 0) {
                g_source_remove (priv->phase_timeout_id);
                priv->phase_timeout_id = 0;
        }

        switch (priv->phase) {
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                do_phase_startup (manager);
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                g_signal_emit (manager, signals[SESSION_RUNNING], 0);
                update_idle (manager);
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                do_phase_query_end_session (manager);
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                do_phase_end_session (manager);
                break;
        case CSM_MANAGER_PHASE_EXIT:
                do_phase_exit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
_debug_app_for_phase (const char *id,
                      CsmApp     *app,
                      gpointer    data)
{
        guint phase;

        phase = GPOINTER_TO_UINT (data);

        if (csm_app_peek_phase (app) != phase) {
                return FALSE;
        }

        g_debug ("CsmManager:\tID: %s\tapp-id:%s\tis-disabled:%d\tis-conditionally-disabled:%d\tis-delayed:%d",
                 csm_app_peek_id (app),
                 csm_app_peek_app_id (app),
                 csm_app_peek_is_disabled (app),
                 csm_app_peek_is_conditionally_disabled (app),
                 (csm_app_peek_autostart_delay (app) > 0));

        return FALSE;
}

static void
debug_app_summary (CsmManager *manager)
{
        guint phase;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        g_debug ("CsmManager: App startup summary");
        for (phase = CSM_MANAGER_PHASE_INITIALIZATION; phase < CSM_MANAGER_PHASE_RUNNING; phase++) {
                g_debug ("CsmManager: Phase %s", phase_num_to_name (phase));
                csm_store_foreach (priv->apps,
                                   (CsmStoreFunc)_debug_app_for_phase,
                                   GUINT_TO_POINTER (phase));
        }
}

void
csm_manager_start (CsmManager *manager)
{
        g_debug ("CsmManager: CSM starting to manage");

        g_return_if_fail (CSM_IS_MANAGER (manager));

        csm_manager_set_phase (manager, CSM_MANAGER_PHASE_INITIALIZATION);
        debug_app_summary (manager);
        start_phase (manager);
}

void
_csm_manager_set_renderer (CsmManager *manager,
                           const char *renderer)
{
        CsmManagerPrivate *priv;
        priv = csm_manager_get_instance_private (manager);
        priv->renderer = renderer;
}

static gboolean
_app_has_app_id (const char   *id,
                 CsmApp       *app,
                 const char   *app_id_a)
{
        const char *app_id_b;

        app_id_b = csm_app_peek_app_id (app);
        return (app_id_b != NULL && strcmp (app_id_a, app_id_b) == 0);
}

static CsmApp *
find_app_for_app_id (CsmManager *manager,
                     const char *app_id)
{
        CsmApp *app;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        app = (CsmApp *)csm_store_find (priv->apps,
                                        (CsmStoreFunc)_app_has_app_id,
                                        (char *)app_id);
        return app;
}

static gboolean
inhibitor_has_client_id (gpointer      key,
                         CsmInhibitor *inhibitor,
                         const char   *client_id_a)
{
        gboolean    matches;
        const char *client_id_b;

        client_id_b = csm_inhibitor_peek_client_id (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (client_id_a) && ! IS_STRING_EMPTY (client_id_b)) {
                matches = (strcmp (client_id_a, client_id_b) == 0);
                if (matches) {
                        g_debug ("CsmManager: removing JIT inhibitor for %s for reason '%s'",
                                 csm_inhibitor_peek_client_id (inhibitor),
                                 csm_inhibitor_peek_reason (inhibitor));
                }
        }

        return matches;
}

static gboolean
_app_has_startup_id (const char *id,
                     CsmApp     *app,
                     const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = csm_app_peek_startup_id (app);

        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static CsmApp *
find_app_for_startup_id (CsmManager *manager,
                        const char *startup_id)
{
        CsmApp *found_app;
        GSList *a;
        CsmManagerPrivate *priv;

        found_app = NULL;
        priv = csm_manager_get_instance_private (manager);

        /* If we're starting up the session, try to match the new client
         * with one pending apps for the current phase. If not, try to match
         * with any of the autostarted apps. */
        if (priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                for (a = priv->pending_apps; a != NULL; a = a->next) {
                        CsmApp *app = CSM_APP (a->data);

                        if (strcmp (startup_id, csm_app_peek_startup_id (app)) == 0) {
                                found_app = app;
                                goto out;
                        }
                }
        } else {
                CsmApp *app;

                app = (CsmApp *)csm_store_find (priv->apps,
                                                (CsmStoreFunc)_app_has_startup_id,
                                                (char *)startup_id);
                if (app != NULL) {
                        found_app = app;
                        goto out;
                }
        }
 out:
        return found_app;
}

static void
_disconnect_client (CsmManager *manager,
                    CsmClient  *client)
{
        gboolean              is_condition_client;
        CsmApp               *app;
        GError               *error;
        gboolean UNUSED_VARIABLE res;
        const char           *app_id;
        const char           *startup_id;
        gboolean              app_restart;
        CsmClientRestartStyle client_restart_hint;
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: disconnect client: %s", csm_client_peek_id (client));

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        csm_client_set_status (client, CSM_CLIENT_FINISHED);

        is_condition_client = FALSE;
        priv = csm_manager_get_instance_private (manager);
        if (g_slist_find (priv->condition_clients, client)) {
                priv->condition_clients = g_slist_remove (priv->condition_clients, client);

                is_condition_client = TRUE;
        }

        /* remove any inhibitors for this client */
        csm_store_foreach_remove (priv->inhibitors,
                                  (CsmStoreFunc)inhibitor_has_client_id,
                                  (gpointer)csm_client_peek_id (client));

        app = NULL;

        /* first try to match on startup ID */
        startup_id = csm_client_peek_startup_id (client);
        if (! IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);

        }

        /* then try to find matching app-id */
        if (app == NULL) {
                app_id = csm_client_peek_app_id (client);
                if (! IS_STRING_EMPTY (app_id)) {
                        g_debug ("CsmManager: disconnect for app '%s'", app_id);
                        app = find_app_for_app_id (manager, app_id);
                }
        }

        if (priv->phase == CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Instead of answering our end session query, the client just exited.
                 * Treat that as an "okay, end the session" answer.
                 *
                 * This call implicitly removes any inhibitors for the client, along
                 * with removing the client from the pending query list.
                 */
                _handle_client_end_session_response (manager,
                                                     client,
                                                     TRUE,
                                                     FALSE,
                                                     FALSE,
                                                     "Client exited in "
                                                     "query end session phase "
                                                     "instead of end session "
                                                     "phase");
        }

        if (priv->dbus_disconnected && CSM_IS_DBUS_CLIENT (client)) {
                g_debug ("CsmManager: dbus disconnected, not restarting application");
                goto out;
        }


        if (app == NULL) {
                g_debug ("CsmManager: unable to find application for client - not restarting");
                goto out;
        }

        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("CsmManager: in shutdown, not restarting application");
                goto out;
        }

        app_restart = csm_app_peek_autorestart (app);
        client_restart_hint = csm_client_peek_restart_style_hint (client);

        /* allow legacy clients to override the app info */
        if (! app_restart
            && client_restart_hint != CSM_CLIENT_RESTART_IMMEDIATELY) {
                g_debug ("CsmManager: autorestart not set, not restarting application");
                goto out;
        }

        if (is_condition_client) {
                g_debug ("CsmManager: app conditionally disabled, not restarting application");
                goto out;
        }

        g_debug ("CsmManager: restarting app");

        error = NULL;
        res = csm_app_restart (app, &error);
        if (error != NULL) {
                g_warning ("Error on restarting session managed app: %s", error->message);
                g_error_free (error);
        }

 out:
        g_object_unref (client);
}

typedef struct {
        const char *service_name;
        CsmManager *manager;
} RemoveClientData;

static gboolean
_disconnect_dbus_client (const char       *id,
                         CsmClient        *client,
                         RemoveClientData *data)
{
        const char *name;

        if (! CSM_IS_DBUS_CLIENT (client)) {
                return FALSE;
        }

        /* If no service name, then we simply disconnect all clients */
        if (!data->service_name) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        name = csm_dbus_client_get_bus_name (CSM_DBUS_CLIENT (client));
        if (IS_STRING_EMPTY (name)) {
                return FALSE;
        }

        if (strcmp (data->service_name, name) == 0) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        return FALSE;
}

/**
 * remove_clients_for_connection:
 * @manager: a #CsmManager
 * @service_name: a service name
 *
 * Disconnects clients that own @service_name.
 *
 * If @service_name is NULL, then disconnects all clients for the connection.
 */
static void
remove_clients_for_connection (CsmManager *manager,
                               const char *service_name)
{
        RemoveClientData data;
        CsmManagerPrivate *priv;

        data.service_name = service_name;
        data.manager = manager;
        priv = csm_manager_get_instance_private (manager);

        /* disconnect dbus clients for name */
        csm_store_foreach_remove (priv->clients,
                                  (CsmStoreFunc)_disconnect_dbus_client,
                                  &data);

        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION
            && csm_store_size (priv->clients) == 0) {
                g_debug ("CsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
inhibitor_has_bus_name (gpointer          key,
                        CsmInhibitor     *inhibitor,
                        RemoveClientData *data)
{
        gboolean    matches;
        const char *bus_name_b;

        bus_name_b = csm_inhibitor_peek_bus_name (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (data->service_name) && ! IS_STRING_EMPTY (bus_name_b)) {
                matches = (strcmp (data->service_name, bus_name_b) == 0);
                if (matches) {
                        g_debug ("CsmManager: removing inhibitor from %s for reason '%s' on connection %s",
                                 csm_inhibitor_peek_app_id (inhibitor),
                                 csm_inhibitor_peek_reason (inhibitor),
                                 csm_inhibitor_peek_bus_name (inhibitor));
                }
        }

        return matches;
}

static void
remove_inhibitors_for_connection (CsmManager *manager,
                                  const char *service_name)
{
        guint UNUSED_VARIABLE n_removed;
        RemoveClientData data;
        CsmManagerPrivate *priv;

        data.service_name = service_name;
        data.manager = manager;
        priv = csm_manager_get_instance_private (manager);

        debug_inhibitors (manager);

        n_removed = csm_store_foreach_remove (priv->inhibitors,
                                              (CsmStoreFunc)inhibitor_has_bus_name,
                                              &data);
}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        CsmManager  *manager)
{
        if (strlen (new_service_name) == 0
            && strlen (old_service_name) > 0) {
                /* service removed */
                remove_inhibitors_for_connection (manager, old_service_name);
                remove_clients_for_connection (manager, old_service_name);
        } else if (strlen (old_service_name) == 0
                   && strlen (new_service_name) > 0) {
                /* service added */

                /* use this if we support automatically registering
                 * well known bus names */
        }
}

static DBusHandlerResult
csm_manager_bus_filter (DBusConnection *connection,
                        DBusMessage    *message,
                        void           *user_data)
{
        CsmManager *manager;
        CsmManagerPrivate *priv;

        manager = CSM_MANAGER (user_data);
        priv = csm_manager_get_instance_private (manager);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                g_debug ("CsmManager: dbus disconnected; disconnecting dbus clients...");
                priv->dbus_disconnected = TRUE;
                remove_clients_for_connection (manager, NULL);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
register_manager (CsmManager *manager)
{
        GError *error = NULL;
        CsmManagerPrivate *priv;
        DBusConnection *connection;

        error = NULL;
        priv = csm_manager_get_instance_private (manager);

        priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        connection = dbus_g_connection_get_connection (priv->connection);
        dbus_connection_add_filter (connection,
                                    csm_manager_bus_filter,
                                    manager, NULL);
        priv->dbus_disconnected = FALSE;

        priv->bus_proxy = dbus_g_proxy_new_for_name (priv->connection,
                                                     DBUS_SERVICE_DBUS,
                                                     DBUS_PATH_DBUS,
                                                     DBUS_INTERFACE_DBUS);
        dbus_g_proxy_add_signal (priv->bus_proxy,
                                 "NameOwnerChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (priv->bus_proxy,
                                     "NameOwnerChanged",
                                     G_CALLBACK (bus_name_owner_changed),
                                     manager,
                                     NULL);

        dbus_g_connection_register_g_object (priv->connection, CSM_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

static void
csm_manager_set_failsafe (CsmManager *manager,
                          gboolean    enabled)
{
        CsmManagerPrivate *priv;

        g_return_if_fail (CSM_IS_MANAGER (manager));

        priv = csm_manager_get_instance_private (manager);

        priv->failsafe = enabled;
}

static gboolean
_client_has_startup_id (const char *id,
                        CsmClient  *client,
                        const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = csm_client_peek_startup_id (client);

        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static void
on_client_disconnected (CsmClient  *client,
                        CsmManager *manager)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: disconnect client");

        priv = csm_manager_get_instance_private (manager);

        _disconnect_client (manager, client);
        csm_store_remove (priv->clients, csm_client_peek_id (client));
        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION
            && csm_store_size (priv->clients) == 0) {
                g_debug ("CsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
on_xsmp_client_register_request (CsmXSMPClient *client,
                                 char         **id,
                                 CsmManager    *manager)
{
        gboolean handled;
        char    *new_id;
        CsmApp  *app;
        CsmManagerPrivate *priv;

        handled = TRUE;
        new_id = NULL;
        priv = csm_manager_get_instance_private (manager);

        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                goto out;
        }

        if (IS_STRING_EMPTY (*id)) {
                new_id = csm_util_generate_startup_id ();
        } else {
                CsmClient *client;

                client = (CsmClient *)csm_store_find (priv->clients,
                                                      (CsmStoreFunc)_client_has_startup_id,
                                                      *id);
                /* We can't have two clients with the same id. */
                if (client != NULL) {
                        goto out;
                }

                new_id = g_strdup (*id);
        }

        g_debug ("CsmManager: Adding new client %s to session", new_id);

        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        /* If it's a brand new client id, we just accept the client*/
        if (IS_STRING_EMPTY (*id)) {
                goto out;
        }

        app = find_app_for_startup_id (manager, new_id);
        if (app != NULL) {
                csm_client_set_app_id (CSM_CLIENT (client), csm_app_peek_app_id (app));
                csm_app_registered (app);
                goto out;
        }

        /* app not found */
        g_free (new_id);
        new_id = NULL;

 out:
        g_free (*id);
        *id = new_id;

        return handled;
}

static gboolean
auto_save_is_enabled (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        return g_settings_get_boolean (priv->settings_session,
                                       KEY_AUTOSAVE);
}

static void
maybe_save_session (CsmManager *manager)
{
        CsmConsolekit *consolekit = NULL;
#ifdef HAVE_SYSTEMD
        CsmSystemd *systemd = NULL;
#endif
        char *session_type;
        GError *error;
        CsmManagerPrivate *priv;

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {
                systemd = csm_get_systemd ();
                session_type = csm_systemd_get_current_session_type (systemd);

                if (g_strcmp0 (session_type, CSM_SYSTEMD_SESSION_TYPE_LOGIN_WINDOW) == 0) {
                        goto out;
                }
        }
        else {
#endif
        consolekit = csm_get_consolekit ();
        session_type = csm_consolekit_get_current_session_type (consolekit);

        if (g_strcmp0 (session_type, CSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) == 0) {
                goto out;
        }
#ifdef HAVE_SYSTEMD
        }
#endif

        priv = csm_manager_get_instance_private (manager);
        /* We only allow session saving when session is running or when
         * logging out */
        if (priv->phase != CSM_MANAGER_PHASE_RUNNING &&
            priv->phase != CSM_MANAGER_PHASE_END_SESSION) {
                goto out;
        }

        error = NULL;
        csm_session_save (priv->clients, &error);

        if (error) {
                g_warning ("Error saving session: %s", error->message);
                g_error_free (error);
        }

out:
        if (consolekit != NULL)
                g_object_unref (consolekit);
#ifdef HAVE_SYSTEMD
        if (systemd != NULL)
                g_object_unref (systemd);
#endif
        g_free (session_type);
}

static void
_handle_client_end_session_response (CsmManager *manager,
                                     CsmClient  *client,
                                     gboolean    is_ok,
                                     gboolean    do_last,
                                     gboolean    cancel,
                                     const char *reason)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* just ignore if received outside of shutdown */
        if (priv->phase < CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        g_debug ("CsmManager: Response from end session request: is-ok=%d do-last=%d cancel=%d reason=%s", is_ok, do_last, cancel, reason ? reason :"");

        if (cancel) {
                cancel_end_session (manager);
                return;
        }

        priv->query_clients = g_slist_remove (priv->query_clients, client);

        if (! is_ok && priv->logout_mode != CSM_MANAGER_LOGOUT_MODE_FORCE) {
                guint         cookie;
                CsmInhibitor *inhibitor;
                char         *app_id;
                const char   *bus_name;

                /* FIXME: do we support updating the reason? */

                /* Create JIT inhibit */
                if (CSM_IS_DBUS_CLIENT (client)) {
                        bus_name = csm_dbus_client_get_bus_name (CSM_DBUS_CLIENT (client));
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (csm_client_peek_app_id (client));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = csm_client_get_app_name (client);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = csm_inhibitor_new_for_client (csm_client_peek_id (client),
                                                          app_id,
                                                          CSM_INHIBITOR_FLAG_LOGOUT,
                                                          reason != NULL ? reason : _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                csm_store_add (priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        } else {
                csm_store_foreach_remove (priv->inhibitors,
                                          (CsmStoreFunc)inhibitor_has_client_id,
                                          (gpointer)csm_client_peek_id (client));
        }

        if (priv->phase == CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                if (priv->query_clients == NULL) {
                        query_end_session_complete (manager);
                }
        } else if (priv->phase == CSM_MANAGER_PHASE_END_SESSION) {
                if (do_last) {
                        /* This only makes sense if we're in part 1 of
                         * CSM_MANAGER_PHASE_END_SESSION. Doing this in part 2
                         * can only happen because of a buggy client that loops
                         * wanting to be last again and again. The phase
                         * timeout will take care of this issue. */
                        priv->next_query_clients = g_slist_prepend (priv->next_query_clients,
                                                                    client);
                }

                /* we can continue to the next step if all clients have replied
                 * and if there's no inhibitor */
                if (priv->query_clients != NULL
                    || csm_manager_is_logout_inhibited (manager)) {
                        return;
                }

                if (priv->next_query_clients != NULL) {
                        do_phase_end_session_part_2 (manager);
                } else {
                        end_phase (manager);
                }
        }
}

static void
on_client_end_session_response (CsmClient  *client,
                                gboolean    is_ok,
                                gboolean    do_last,
                                gboolean    cancel,
                                const char *reason,
                                CsmManager *manager)
{
        _handle_client_end_session_response (manager,
                                             client,
                                             is_ok,
                                             do_last,
                                             cancel,
                                             reason);
}

static void
on_xsmp_client_logout_request (CsmXSMPClient *client,
                               gboolean       show_dialog,
                               CsmManager    *manager)
{
        GError *error;
        int     logout_mode;

        if (show_dialog) {
                logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;
        } else {
                logout_mode = CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION;
        }

        error = NULL;
        csm_manager_logout (manager, logout_mode, &error);
        if (error != NULL) {
                g_warning ("Unable to logout: %s", error->message);
                g_error_free (error);
        }
}

static void
on_store_client_added (CsmStore   *store,
                       const char *id,
                       CsmManager *manager)
{
        CsmClient *client;

        g_debug ("CsmManager: Client added: %s", id);

        client = (CsmClient *)csm_store_lookup (store, id);

        /* a bit hacky */
        if (CSM_IS_XSMP_CLIENT (client)) {
                g_signal_connect (client,
                                  "register-request",
                                  G_CALLBACK (on_xsmp_client_register_request),
                                  manager);
                g_signal_connect (client,
                                  "logout-request",
                                  G_CALLBACK (on_xsmp_client_logout_request),
                                  manager);
        }

        g_signal_connect (client,
                          "end-session-response",
                          G_CALLBACK (on_client_end_session_response),
                          manager);

        g_signal_emit (manager, signals [CLIENT_ADDED], 0, id);
        /* FIXME: disconnect signal handler */
}

static void
on_store_client_removed (CsmStore   *store,
                         const char *id,
                         CsmManager *manager)
{
        g_debug ("CsmManager: Client removed: %s", id);

        g_signal_emit (manager, signals [CLIENT_REMOVED], 0, id);
}

static void
csm_manager_set_client_store (CsmManager *manager,
                              CsmStore   *store)
{
        CsmManagerPrivate *priv;

        g_return_if_fail (CSM_IS_MANAGER (manager));
        priv = csm_manager_get_instance_private (manager);

        if (store != NULL) {
                g_object_ref (store);
        }

        if (priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_removed,
                                                      manager);

                g_object_unref (priv->clients);
        }


        g_debug ("CsmManager: setting client store %p", store);

        priv->clients = store;

        if (priv->clients != NULL) {
                g_signal_connect (priv->clients,
                                  "added",
                                  G_CALLBACK (on_store_client_added),
                                  manager);
                g_signal_connect (priv->clients,
                                  "removed",
                                  G_CALLBACK (on_store_client_removed),
                                  manager);
        }
}

static void
csm_manager_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        CsmManager *self;

        self = CSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                csm_manager_set_failsafe (self, g_value_get_boolean (value));
                break;
         case PROP_CLIENT_STORE:
                csm_manager_set_client_store (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        CsmManager *self;
        CsmManagerPrivate *priv;

        self = CSM_MANAGER (object);
        priv = csm_manager_get_instance_private (self);

        switch (prop_id) {
        case PROP_FAILSAFE:
                g_value_set_boolean (value, priv->failsafe);
                break;
        case PROP_CLIENT_STORE:
                g_value_set_object (value, priv->clients);
                break;
        case PROP_RENDERER:
                g_value_set_string (value, priv->renderer);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
_find_app_provides (const char *id,
                    CsmApp     *app,
                    const char *service)
{
        return csm_app_provides (app, service);
}

static GObject *
csm_manager_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        CsmManager *manager;

        manager = CSM_MANAGER (G_OBJECT_CLASS (csm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));
        return G_OBJECT (manager);
}

static void
on_store_inhibitor_added (CsmStore   *store,
                          const char *id,
                          CsmManager *manager)
{
        g_debug ("CsmManager: Inhibitor added: %s", id);
        g_signal_emit (manager, signals [INHIBITOR_ADDED], 0, id);
        update_idle (manager);
}

static void
on_store_inhibitor_removed (CsmStore   *store,
                            const char *id,
                            CsmManager *manager)
{
        g_debug ("CsmManager: Inhibitor removed: %s", id);
        g_signal_emit (manager, signals [INHIBITOR_REMOVED], 0, id);
        update_idle (manager);
}

static void
csm_manager_dispose (GObject *object)
{
        CsmManagerPrivate *priv;
        CsmManager *manager = CSM_MANAGER (object);

        g_debug ("CsmManager: disposing manager");

        priv = csm_manager_get_instance_private (manager);

        if (priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->clients,
                                                      on_store_client_removed,
                                                      manager);
                g_object_unref (priv->clients);
                priv->clients = NULL;
        }

        if (priv->apps != NULL) {
                g_object_unref (priv->apps);
                priv->apps = NULL;
        }

        if (priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (priv->inhibitors,
                                                      on_store_inhibitor_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (priv->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      manager);

                g_object_unref (priv->inhibitors);
                priv->inhibitors = NULL;
        }

        if (priv->presence != NULL) {
                g_object_unref (priv->presence);
                priv->presence = NULL;
        }

        if (priv->settings_session) {
                g_object_unref (priv->settings_session);
                priv->settings_session = NULL;
        }

        if (priv->settings_lockdown) {
                g_object_unref (priv->settings_lockdown);
                priv->settings_lockdown = NULL;
        }

        if (priv->settings_screensaver) {
                g_object_unref (priv->settings_screensaver);
                priv->settings_screensaver = NULL;
        }
        G_OBJECT_CLASS (csm_manager_parent_class)->dispose (object);
}

static void
csm_manager_class_init (CsmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = csm_manager_get_property;
        object_class->set_property = csm_manager_set_property;
        object_class->constructor = csm_manager_constructor;
        object_class->finalize = csm_manager_finalize;
        object_class->dispose = csm_manager_dispose;

        signals [PHASE_CHANGED] =
                g_signal_new ("phase-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, phase_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals [SESSION_RUNNING] =
                g_signal_new ("session-running",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, session_running),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [SESSION_OVER] =
                g_signal_new ("session-over",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, session_over),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [CLIENT_ADDED] =
                g_signal_new ("client-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, client_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [CLIENT_REMOVED] =
                g_signal_new ("client-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, client_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [INHIBITOR_ADDED] =
                g_signal_new ("inhibitor-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, inhibitor_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [INHIBITOR_REMOVED] =
                g_signal_new ("inhibitor-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, inhibitor_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);

        g_object_class_install_property (object_class,
                                         PROP_FAILSAFE,
                                         g_param_spec_boolean ("failsafe",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_STORE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              CSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_RENDERER,
                                         g_param_spec_string ("renderer",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE));

        dbus_g_object_type_install_info (CSM_TYPE_MANAGER, &dbus_glib_csm_manager_object_info);
        dbus_g_error_domain_register (CSM_MANAGER_ERROR, NULL, CSM_MANAGER_TYPE_ERROR);
}

static void
load_idle_delay_from_gsettings (CsmManager *manager)
{
        glong   value;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        value = g_settings_get_int (priv->settings_session,
                                    KEY_IDLE_DELAY);
        csm_presence_set_idle_timeout (priv->presence, value * 60000);
}

static void
on_gsettings_key_changed (GSettings   *settings,
                          gchar       *key,
                          CsmManager  *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (g_strcmp0 (key, KEY_IDLE_DELAY) == 0) {
                int delay;
                delay = g_settings_get_int (settings, key);
                csm_presence_set_idle_timeout (priv->presence, delay * 60000);
        } else if (g_strcmp0 (key, KEY_LOCK_DISABLE) == 0) {
                /* ??? */
                gboolean UNUSED_VARIABLE disabled;
                disabled = g_settings_get_boolean (settings, key);
        } else if (g_strcmp0 (key, KEY_USER_SWITCH_DISABLE) == 0) {
                /* ??? */
                gboolean UNUSED_VARIABLE disabled;
                disabled = g_settings_get_boolean (settings, key);
        } else {
                g_debug ("Config key not handled: %s", key);
        }
}

static void
on_presence_status_changed (CsmPresence  *presence,
                            guint         status,
                            CsmManager   *manager)
{
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {
                CsmSystemd *systemd;

                systemd = csm_get_systemd ();
                csm_systemd_set_session_idle (systemd,
                                              (status == CSM_PRESENCE_STATUS_IDLE));
        }
        else {
#endif
        CsmConsolekit *consolekit;

        consolekit = csm_get_consolekit ();
        csm_consolekit_set_session_idle (consolekit,
                                         (status == CSM_PRESENCE_STATUS_IDLE));
#ifdef HAVE_SYSTEMD
        }
#endif
}

static void
csm_manager_init (CsmManager *manager)
{
        gchar **schemas = NULL;
        gboolean schema_exists;
        guint i;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        priv->settings_session = g_settings_new (SESSION_SCHEMA);
        priv->settings_lockdown = g_settings_new (LOCKDOWN_SCHEMA);

        /* check if cafe-screensaver is installed */
        g_settings_schema_source_list_schemas (g_settings_schema_source_get_default (), TRUE, &schemas, NULL);
        schema_exists = FALSE;
        for (i = 0; schemas[i] != NULL; i++) {
                if (g_str_equal (schemas[i], SCREENSAVER_SCHEMA)) {
                        schema_exists = TRUE;
                        break;
                }
        }

        g_strfreev (schemas);

        if (schema_exists == TRUE)
                priv->settings_screensaver = g_settings_new (SCREENSAVER_SCHEMA);
        else
                priv->settings_screensaver = NULL;

        priv->inhibitors = csm_store_new ();
        g_signal_connect (priv->inhibitors,
                          "added",
                          G_CALLBACK (on_store_inhibitor_added),
                          manager);
        g_signal_connect (priv->inhibitors,
                          "removed",
                          G_CALLBACK (on_store_inhibitor_removed),
                          manager);

        priv->apps = csm_store_new ();

        priv->presence = csm_presence_new ();
        g_signal_connect (priv->presence,
                          "status-changed",
                          G_CALLBACK (on_presence_status_changed),
                          manager);
        g_signal_connect (priv->settings_session,
                          "changed",
                          G_CALLBACK (on_gsettings_key_changed),
                          manager);
        g_signal_connect (priv->settings_lockdown,
                          "changed",
                          G_CALLBACK (on_gsettings_key_changed),
                          manager);

        load_idle_delay_from_gsettings (manager);
}

static void
csm_manager_finalize (GObject *object)
{
        CsmManager *manager;
        CsmManagerPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_MANAGER (object));

        manager = CSM_MANAGER (object);
        priv = csm_manager_get_instance_private (manager);

        g_return_if_fail (priv != NULL);

        G_OBJECT_CLASS (csm_manager_parent_class)->finalize (object);
}

CsmManager *
csm_manager_new (CsmStore *client_store,
                 gboolean  failsafe)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (CSM_TYPE_MANAGER,
                                               "client-store", client_store,
                                               "failsafe", failsafe,
                                               NULL);

                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return CSM_MANAGER (manager_object);
}

gboolean
csm_manager_setenv (CsmManager  *manager,
                    const char  *variable,
                    const char  *value,
                    GError     **error)
{
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase > CSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "Setenv interface is only available during the Initialization phase");
                return FALSE;
        }

        csm_util_setenv (variable, value);

        return TRUE;
}

gboolean
csm_manager_initialization_error (CsmManager  *manager,
                                  const char  *message,
                                  gboolean     fatal,
                                  GError     **error)
{
        CsmManagerPrivate *priv;
        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);

        if (priv->phase > CSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "InitializationError interface is only available during the Initialization phase");
                return FALSE;
        }

        csm_util_init_error (fatal, "%s", message);

        return TRUE;
}

static gboolean
csm_manager_is_switch_user_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_SWITCH_USER));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
csm_manager_is_suspend_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_SUSPEND));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static void
request_reboot_privileges_completed_consolekit (CsmConsolekit *consolekit,
                                                gboolean       success,
                                                gboolean       ask_later,
                                                GError        *error,
                                                CsmManager    *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* make sure we disconnect the signal handler so that it's not called
         * again next time the event is fired -- this can happen if the reboot
         * is cancelled. */
        g_signal_handlers_disconnect_by_func (consolekit,
                                              request_reboot_privileges_completed_consolekit,
                                              manager);

        g_object_unref (consolekit);

        if (success) {
                if (ask_later) {
                        priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT_INTERACT;
                } else {
                        priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT;
                }

                end_phase (manager);
        }
}

#ifdef HAVE_SYSTEMD
static void
request_reboot_privileges_completed_systemd (CsmSystemd *systemd,
                                             gboolean    success,
                                             gboolean    ask_later,
                                             GError     *error,
                                             CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* make sure we disconnect the signal handler so that it's not called
         * again next time the event is fired -- this can happen if the reboot
         * is cancelled. */
        g_signal_handlers_disconnect_by_func (systemd,
                                              request_reboot_privileges_completed_systemd,
                                              manager);

        g_object_unref (systemd);

        if (success) {
                if (ask_later) {
                        priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT_INTERACT;
                } else {
                        priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT;
                }

                end_phase (manager);
        }
}
#endif

static void
request_reboot (CsmManager *manager)
{
        CsmConsolekit *consolekit;
#ifdef HAVE_SYSTEMD
        CsmSystemd *systemd;
#endif
        gboolean       success;
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting reboot");

        priv = csm_manager_get_instance_private (manager);

        /* We request the privileges before doing anything. There are a few
         * different cases here:
         *
         *   - no systemd: we fallback on ConsoleKit
         *   - no ConsoleKit: we fallback on CDM
         *   - no password required: everything is fine
         *   - password asked once: we ask for it now. If the user enters it
         *     fine, then all is great. If the user doesn't enter it fine, we
         *     don't do anything (so no logout).
         *   - password asked each time: we don't ask it for now since we don't
         *     want to ask for it twice. Instead we'll ask for it at the very
         *     end. If the user will enter it fine, then all is great again. If
         *     the user doesn't enter it fine, then we'll just fallback to CDM.
         *
         * The last case (password asked each time) is a bit broken, but
         * there's really nothing we can do about it. Generally speaking,
         * though, the password will only be asked once (unless the system is
         * configured in paranoid mode), and most probably only if there are
         * more than one user connected. So the general case is that it will
         * just work fine.
         */

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {
                systemd = csm_get_systemd ();
                g_signal_connect (systemd,
                                  "privileges-completed",
                                  G_CALLBACK (request_reboot_privileges_completed_systemd),
                                  manager);
                success = csm_systemd_get_restart_privileges (systemd);

                if (!success) {
                        g_signal_handlers_disconnect_by_func (systemd,
                                                              request_reboot_privileges_completed_systemd,
                                                              manager);
                        g_object_unref (systemd);

                        priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT_CDM;
                        end_phase (manager);
                }
        }
        else {
#endif
        consolekit = csm_get_consolekit ();
        g_signal_connect (consolekit,
                          "privileges-completed",
                          G_CALLBACK (request_reboot_privileges_completed_consolekit),
                          manager);
        success = csm_consolekit_get_restart_privileges (consolekit);

        if (!success) {
                g_signal_handlers_disconnect_by_func (consolekit,
                                                      request_reboot_privileges_completed_consolekit,
                                                      manager);
                g_object_unref (consolekit);

                priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT_CDM;
                end_phase (manager);
        }
#ifdef HAVE_SYSTEMD
        }
#endif
}

static void
request_shutdown_privileges_completed_consolekit (CsmConsolekit *consolekit,
                                                  gboolean       success,
                                                  gboolean       ask_later,
                                                  GError        *error,
                                                  CsmManager    *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* make sure we disconnect the signal handler so that it's not called
         * again next time the event is fired -- this can happen if the reboot
         * is cancelled. */
        g_signal_handlers_disconnect_by_func (consolekit,
                                              request_shutdown_privileges_completed_consolekit,
                                              manager);

        g_object_unref (consolekit);

        if (success) {
                if (ask_later) {
                        priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT;
                } else {
                        priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN;
                }

                end_phase (manager);
        }
}

#ifdef HAVE_SYSTEMD
static void
request_shutdown_privileges_completed_systemd (CsmSystemd *systemd,
                                               gboolean    success,
                                               gboolean    ask_later,
                                               GError     *error,
                                               CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* make sure we disconnect the signal handler so that it's not called
         * again next time the event is fired -- this can happen if the reboot
         * is cancelled. */
        g_signal_handlers_disconnect_by_func (systemd,
                                              request_shutdown_privileges_completed_systemd,
                                              manager);

        g_object_unref (systemd);

        if (success) {
                if (ask_later) {
                        priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT;
                } else {
                        priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN;
                }

                end_phase (manager);
        }
}
#endif

static void
request_shutdown (CsmManager *manager)
{
        CsmConsolekit *consolekit;
#ifdef HAVE_SYSTEMD
        CsmSystemd *systemd;
#endif
        gboolean       success;
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting shutdown");

        priv = csm_manager_get_instance_private (manager);

        /* See the comment in request_reboot() for some more details about how
         * this works. */

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {
                systemd = csm_get_systemd ();
                g_signal_connect (systemd,
                                  "privileges-completed",
                                  G_CALLBACK (request_shutdown_privileges_completed_systemd),
                                  manager);
                success = csm_systemd_get_stop_privileges (systemd);

                if (!success) {
                        g_signal_handlers_disconnect_by_func (systemd,
                                                              request_shutdown_privileges_completed_systemd,
                                                              manager);
                        g_object_unref (systemd);

                        priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN_CDM;
                        end_phase (manager);
                }
        }
        else {
#endif
        consolekit = csm_get_consolekit ();
        g_signal_connect (consolekit,
                          "privileges-completed",
                          G_CALLBACK (request_shutdown_privileges_completed_consolekit),
                          manager);
        success = csm_consolekit_get_stop_privileges (consolekit);

        if (!success) {
                g_signal_handlers_disconnect_by_func (consolekit,
                                                      request_shutdown_privileges_completed_consolekit,
                                                      manager);
                g_object_unref (consolekit);

                priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN_CDM;
                end_phase (manager);
        }
#ifdef HAVE_SYSTEMD
        }
#endif
}

static void
request_suspend (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting suspend");

        if (! csm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_suspend (manager);
                return;
        }

        priv = csm_manager_get_instance_private (manager);
        if (priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                ctk_window_present (CTK_WINDOW (priv->inhibit_dialog));
                return;
        }

        priv->inhibit_dialog = csm_inhibit_dialog_new (priv->inhibitors,
                                                       priv->clients,
                                                       CSM_LOGOUT_ACTION_SLEEP);

        g_signal_connect (priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        ctk_widget_show (priv->inhibit_dialog);
}

static void
request_hibernate (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting hibernate");

        /* hibernate uses suspend inhibit */
        if (! csm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_hibernate (manager);
                return;
        }

        priv = csm_manager_get_instance_private (manager);
        if (priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                ctk_window_present (CTK_WINDOW (priv->inhibit_dialog));
                return;
        }

        priv->inhibit_dialog = csm_inhibit_dialog_new (priv->inhibitors,
                                                       priv->clients,
                                                       CSM_LOGOUT_ACTION_HIBERNATE);

        g_signal_connect (priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        ctk_widget_show (priv->inhibit_dialog);
}


static void
request_logout (CsmManager            *manager,
                CsmManagerLogoutMode  mode)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting logout");

        priv = csm_manager_get_instance_private (manager);

        priv->logout_mode = mode;
        priv->logout_type = CSM_MANAGER_LOGOUT_LOGOUT;

        end_phase (manager);
}

static void
request_switch_user (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: requesting user switch");

        /* See comment in manager_switch_user() to understand why we do this in
         * both functions. */
        if (_switch_user_is_locked_down (manager)) {
                g_warning ("Unable to switch user: User switching has been locked down");
                return;
        }

        if (! csm_manager_is_switch_user_inhibited (manager)) {
                manager_switch_user (manager);
                return;
        }

        priv = csm_manager_get_instance_private (manager);
        if (priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                ctk_window_present (CTK_WINDOW (priv->inhibit_dialog));
                return;
        }

        priv->inhibit_dialog = csm_inhibit_dialog_new (priv->inhibitors,
                                                       priv->clients,
                                                       CSM_LOGOUT_ACTION_SWITCH_USER);

        g_signal_connect (priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        ctk_widget_show (priv->inhibit_dialog);
}

static void
logout_dialog_response (CsmLogoutDialog *logout_dialog,
                        guint            response_id,
                        CsmManager      *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        /* We should only be here if mode has already have been set from
         * show_fallback_shutdown/logout_dialog
         */
        g_assert (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_NORMAL);

        g_debug ("CsmManager: Logout dialog response: %d", response_id);

        ctk_widget_destroy (CTK_WIDGET (logout_dialog));

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case CTK_RESPONSE_CANCEL:
        case CTK_RESPONSE_NONE:
        case CTK_RESPONSE_DELETE_EVENT:
                break;
        case CSM_LOGOUT_RESPONSE_SWITCH_USER:
                request_switch_user (manager);
                break;
        case CSM_LOGOUT_RESPONSE_HIBERNATE:
                request_hibernate (manager);
                break;
        case CSM_LOGOUT_RESPONSE_SLEEP:
                request_suspend (manager);
                break;
        case CSM_LOGOUT_RESPONSE_SHUTDOWN:
                request_shutdown (manager);
                break;
        case CSM_LOGOUT_RESPONSE_REBOOT:
                request_reboot (manager);
                break;
        case CSM_LOGOUT_RESPONSE_LOGOUT:
                /* We've already gotten confirmation from the user so
                 * initiate the logout in NO_CONFIRMATION mode.
                 *
                 * (it shouldn't matter whether we use NO_CONFIRMATION or stay
                 * with NORMAL, unless the shell happens to start after the
                 * user confirmed)
                 */
                request_logout (manager, CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
show_shutdown_dialog (CsmManager *manager)
{
        CtkWidget *dialog;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = csm_get_shutdown_dialog (cdk_screen_get_default (),
                                          ctk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        ctk_widget_show (dialog);
        ctk_window_present_with_time (CTK_WINDOW (dialog),
                                      cdk_x11_get_server_time (ctk_widget_get_window (CTK_WIDGET (dialog))));
}

static void
show_logout_dialog (CsmManager *manager)
{
        CtkWidget *dialog;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = csm_get_logout_dialog (cdk_screen_get_default (),
                                        ctk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        ctk_widget_show (dialog);
        ctk_window_present_with_time (CTK_WINDOW (dialog),
                                      cdk_x11_get_server_time (ctk_widget_get_window (CTK_WIDGET (dialog))));
}

static void
user_logout (CsmManager           *manager,
             CsmManagerLogoutMode  mode)
{
        gboolean logout_prompt;
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        logout_prompt =
               g_settings_get_boolean (priv->settings_session,
                                       "logout-prompt");

        /* If the shell isn't running, and this isn't a non-interative logout request,
         * and the user has their settings configured to show a confirmation dialog for
         * logout, then go ahead and show the confirmation dialog now.
         */
        if (mode == CSM_MANAGER_LOGOUT_MODE_NORMAL && logout_prompt) {
                show_logout_dialog (manager);
        } else {
                request_logout (manager, mode);
        }
}

/*
  dbus-send --session --type=method_call --print-reply
      --dest=org.cafe.SessionManager
      /org/gnome/SessionManager
      org.freedesktop.DBus.Introspectable.Introspect
*/

gboolean
csm_manager_set_phase (CsmManager      *manager,
                       CsmManagerPhase  phase)
{
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        priv->phase = phase;
        return (TRUE);
}

gboolean
csm_manager_request_shutdown (CsmManager *manager,
                              GError    **error)
{
        CsmManagerPrivate *priv;
        g_debug ("CsmManager: RequestShutdown called");

        g_return_val_if_fail(CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "RequestShutdown interface is only available during the Running phase");
                return FALSE;
        }

        request_shutdown (manager);

        return TRUE;
}

gboolean
csm_manager_request_reboot (CsmManager *manager,
                            GError    **error)
{
        CsmManagerPrivate *priv;

        g_debug ("CsmManager: RequestReboot called");

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "RequestReboot interface is only available during the running phase");
                return FALSE;
        }

        request_reboot (manager);

        return TRUE;
}

static gboolean
_log_out_is_locked_down (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);

        return g_settings_get_boolean (priv->settings_lockdown,
                                       KEY_LOG_OUT_DISABLE);
}

static gboolean
_switch_user_is_locked_down (CsmManager *manager)
{
        CsmManagerPrivate *priv;

        priv = csm_manager_get_instance_private (manager);
        return g_settings_get_boolean (priv->settings_lockdown,
                                       KEY_USER_SWITCH_DISABLE);
}

gboolean
csm_manager_shutdown (CsmManager *manager,
                      GError    **error)
{
        CsmManagerPrivate *priv;
        g_debug ("CsmManager: Shutdown called");

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Shutdown interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        show_shutdown_dialog (manager);

        return TRUE;
}

gboolean
csm_manager_can_shutdown (CsmManager *manager,
                          gboolean   *shutdown_available,
                          GError    **error)
{
        CsmConsolekit *consolekit;
#ifdef HAVE_SYSTEMD
        CsmSystemd *systemd;
#endif
        g_debug ("CsmManager: CanShutdown called");

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING()) {
                systemd = csm_get_systemd ();
                *shutdown_available = csm_systemd_can_stop (systemd)
                                      || csm_systemd_can_restart (systemd)
                                      || csm_systemd_can_suspend (systemd)
                                      || csm_systemd_can_hibernate (systemd);
                g_object_unref (systemd);
        }
        else {
#endif
        consolekit = csm_get_consolekit ();
        *shutdown_available = !_log_out_is_locked_down (manager) &&
                              (csm_consolekit_can_stop (consolekit)
                               || csm_consolekit_can_restart (consolekit)
                               || csm_consolekit_can_suspend (consolekit)
                               || csm_consolekit_can_hibernate (consolekit));
        g_object_unref (consolekit);
#ifdef HAVE_SYSTEMD
        }
#endif

        return TRUE;
}

gboolean
csm_manager_logout (CsmManager *manager,
                    guint       logout_mode,
                    GError    **error)
{
        CsmManagerPrivate *priv;
        g_debug ("CsmManager: Logout called");

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Shutdown interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        switch (logout_mode) {
        case CSM_MANAGER_LOGOUT_MODE_NORMAL:
        case CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
        case CSM_MANAGER_LOGOUT_MODE_FORCE:
                user_logout (manager, logout_mode);
                break;

        default:
                g_debug ("Unknown logout mode option");

                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_INVALID_OPTION,
                             "Unknown logout mode flag");
                return FALSE;
        }

        return TRUE;
}

gboolean
csm_manager_register_client (CsmManager            *manager,
                             const char            *app_id,
                             const char            *startup_id,
                             DBusGMethodInvocation *context)
{
        char      *new_startup_id;
        char      *sender;
        CsmClient *client;
        CsmApp    *app;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        app = NULL;
        client = NULL;

        g_debug ("CsmManager: RegisterClient %s", startup_id);

        priv = csm_manager_get_instance_private (manager);
        if (priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                GError *new_error;

                g_debug ("Unable to register client: shutting down");

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (startup_id)) {
                new_startup_id = csm_util_generate_startup_id ();
        } else {

                client = (CsmClient *)csm_store_find (priv->clients,
                                                      (CsmStoreFunc)_client_has_startup_id,
                                                      (char *)startup_id);
                /* We can't have two clients with the same startup id. */
                if (client != NULL) {
                        GError *new_error;

                        g_debug ("Unable to register client: already registered");

                        new_error = g_error_new (CSM_MANAGER_ERROR,
                                                 CSM_MANAGER_ERROR_ALREADY_REGISTERED,
                                                 "Unable to register client");
                        dbus_g_method_return_error (context, new_error);
                        g_error_free (new_error);
                        return FALSE;
                }

                new_startup_id = g_strdup (startup_id);
        }

        g_debug ("CsmManager: Adding new client %s to session", new_startup_id);

        if (app == NULL && !IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);
        }
        if (app == NULL && !IS_STRING_EMPTY (app_id)) {
                /* try to associate this app id with a known app */
                app = find_app_for_app_id (manager, app_id);
        }

        sender = dbus_g_method_get_sender (context);
        client = csm_dbus_client_new (new_startup_id, sender);
        g_free (sender);
        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to create client");

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        csm_store_add (priv->clients, csm_client_peek_id (client), G_OBJECT (client));
        /* the store will own the ref */
        g_object_unref (client);

        if (app != NULL) {
                csm_client_set_app_id (client, csm_app_peek_app_id (app));
                csm_app_registered (app);
        } else {
                /* if an app id is specified store it in the client
                   so we can save it later */
                csm_client_set_app_id (client, app_id);
        }

        csm_client_set_status (client, CSM_CLIENT_REGISTERED);

        g_assert (new_startup_id != NULL);
        g_free (new_startup_id);

        dbus_g_method_return (context, csm_client_peek_id (client));

        return TRUE;
}

gboolean
csm_manager_unregister_client (CsmManager            *manager,
                               const char            *client_id,
                               DBusGMethodInvocation *context)
{
        CsmClient *client;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        g_debug ("CsmManager: UnregisterClient %s", client_id);

        priv = csm_manager_get_instance_private (manager);
        client = (CsmClient *)csm_store_lookup (priv->clients, client_id);
        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to unregister client: not registered");

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_NOT_REGISTERED,
                                         "Unable to unregister client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        /* don't disconnect client here, only change the status.
           Wait until it leaves the bus before disconnecting it */
        csm_client_set_status (client, CSM_CLIENT_UNREGISTERED);

        dbus_g_method_return (context);

        return TRUE;
}

gboolean
csm_manager_inhibit (CsmManager            *manager,
                     const char            *app_id,
                     guint                  toplevel_xid,
                     const char            *reason,
                     guint                  flags,
                     DBusGMethodInvocation *context)
{
        CsmInhibitor *inhibitor;
        guint         cookie;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        g_debug ("CsmManager: Inhibit xid=%u app_id=%s reason=%s flags=%u",
                 toplevel_xid,
                 app_id,
                 reason,
                 flags);

        priv = csm_manager_get_instance_private (manager);
        if (priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                GError *new_error;

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Forced logout cannot be inhibited");
                g_debug ("CsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (app_id)) {
                GError *new_error;

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Application ID not specified");
                g_debug ("CsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (reason)) {
                GError *new_error;

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Reason not specified");
                g_debug ("CsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (flags == 0) {
                GError *new_error;

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Invalid inhibit flags");
                g_debug ("CsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = csm_inhibitor_new (app_id,
                                       toplevel_xid,
                                       flags,
                                       reason,
                                       dbus_g_method_get_sender (context),
                                       cookie);
        csm_store_add (priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
        g_object_unref (inhibitor);

        dbus_g_method_return (context, cookie);

        return TRUE;
}

gboolean
csm_manager_uninhibit (CsmManager            *manager,
                       guint                  cookie,
                       DBusGMethodInvocation *context)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        g_debug ("CsmManager: Uninhibit %u", cookie);

        priv = csm_manager_get_instance_private (manager);
        inhibitor = (CsmInhibitor *)csm_store_find (priv->inhibitors,
                                                    (CsmStoreFunc)_find_by_cookie,
                                                    &cookie);
        if (inhibitor == NULL) {
                GError *new_error;

                new_error = g_error_new (CSM_MANAGER_ERROR,
                                         CSM_MANAGER_ERROR_GENERAL,
                                         "Unable to uninhibit: Invalid cookie");
                dbus_g_method_return_error (context, new_error);
                g_debug ("Unable to uninhibit: %s", new_error->message);
                g_error_free (new_error);
                return FALSE;
        }

        g_debug ("CsmManager: removing inhibitor %s %u reason '%s' %u connection %s",
                 csm_inhibitor_peek_app_id (inhibitor),
                 csm_inhibitor_peek_toplevel_xid (inhibitor),
                 csm_inhibitor_peek_reason (inhibitor),
                 csm_inhibitor_peek_flags (inhibitor),
                 csm_inhibitor_peek_bus_name (inhibitor));

        csm_store_remove (priv->inhibitors, csm_inhibitor_peek_id (inhibitor));

        dbus_g_method_return (context);

        return TRUE;
}

gboolean
csm_manager_is_inhibited (CsmManager *manager,
                          guint       flags,
                          gboolean   *is_inhibited,
                          GError     *error)
{
        CsmInhibitor *inhibitor;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        if (priv->inhibitors == NULL
            || csm_store_size (priv->inhibitors) == 0) {
                *is_inhibited = FALSE;
                return TRUE;
        }

        inhibitor = (CsmInhibitor *) csm_store_find (priv->inhibitors,
                                                     (CsmStoreFunc)inhibitor_has_flag,
                                                     GUINT_TO_POINTER (flags));
        if (inhibitor == NULL) {
                *is_inhibited = FALSE;
        } else {
                *is_inhibited = TRUE;
        }

        return TRUE;

}

static gboolean
listify_store_ids (char       *id,
                   GObject    *object,
                   GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
        return FALSE;
}

gboolean
csm_manager_get_clients (CsmManager *manager,
                         GPtrArray **clients,
                         GError    **error)
{
        CsmManagerPrivate *priv;
        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        if (clients == NULL) {
                return FALSE;
        }

        *clients = g_ptr_array_new ();
        priv = csm_manager_get_instance_private (manager);
        csm_store_foreach (priv->clients, (CsmStoreFunc)listify_store_ids, clients);

        return TRUE;
}

gboolean
csm_manager_get_inhibitors (CsmManager *manager,
                            GPtrArray **inhibitors,
                            GError    **error)
{
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        if (inhibitors == NULL) {
                return FALSE;
        }

        *inhibitors = g_ptr_array_new ();
        priv = csm_manager_get_instance_private (manager);
        csm_store_foreach (priv->inhibitors,
                           (CsmStoreFunc) listify_store_ids,
                           inhibitors);

        return TRUE;
}


static gboolean
_app_has_autostart_condition (const char *id,
                              CsmApp     *app,
                              const char *condition)
{
        gboolean has;
        gboolean disabled;

        has = csm_app_has_autostart_condition (app, condition);
        disabled = csm_app_peek_is_disabled (app);

        return has && !disabled;
}

gboolean
csm_manager_is_autostart_condition_handled (CsmManager *manager,
                                            const char *condition,
                                            gboolean   *handled,
                                            GError    **error)
{
        CsmApp *app;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        app = (CsmApp *) csm_store_find (priv->apps,(
                                         CsmStoreFunc) _app_has_autostart_condition,
                                         (char *)condition);

        if (app != NULL) {
                *handled = TRUE;
        } else {
                *handled = FALSE;
        }

        return TRUE;
}

static void
append_app (CsmManager *manager,
            CsmApp     *app)
{
        const char *id;
        const char *app_id;
        CsmApp     *dup;
        CsmManagerPrivate *priv;

        id = csm_app_peek_id (app);
        if (IS_STRING_EMPTY (id)) {
                g_debug ("CsmManager: not adding app: no id");
                return;
        }

        priv = csm_manager_get_instance_private (manager);
        dup = (CsmApp *)csm_store_lookup (priv->apps, id);
        if (dup != NULL) {
                g_debug ("CsmManager: not adding app: already added");
                return;
        }

        app_id = csm_app_peek_app_id (app);
        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("CsmManager: not adding app: no app-id");
                return;
        }

        dup = find_app_for_app_id (manager, app_id);
        if (dup != NULL) {
                g_debug ("CsmManager: not adding app: app-id already exists");
                return;
        }

        csm_store_add (priv->apps, id, G_OBJECT (app));
}

gboolean
csm_manager_add_autostart_app (CsmManager *manager,
                               const char *path,
                               const char *provides)
{
        CsmApp *app;
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        priv = csm_manager_get_instance_private (manager);
        /* first check to see if service is already provided */
        if (provides != NULL) {
                CsmApp *dup;

                dup = (CsmApp *)csm_store_find (priv->apps,
                                                (CsmStoreFunc)_find_app_provides,
                                                (char *)provides);
                if (dup != NULL) {
                        g_debug ("CsmManager: service '%s' is already provided", provides);
                        return FALSE;
                }
        }

        app = csm_autostart_app_new (path);
        if (app == NULL) {
                g_warning ("could not read %s", path);
                return FALSE;
        }

        g_debug ("CsmManager: read %s", path);
        append_app (manager, app);
        g_object_unref (app);

        return TRUE;
}

gboolean
csm_manager_add_autostart_apps_from_dir (CsmManager *manager,
                                         const char *path)
{
        GDir       *dir;
        const char *name;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        g_debug ("CsmManager: *** Adding autostart apps for %s", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return FALSE;
        }

        while ((name = g_dir_read_name (dir))) {
                char *desktop_file;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);
                csm_manager_add_autostart_app (manager, desktop_file, NULL);
                g_free (desktop_file);
        }

        g_dir_close (dir);

        return TRUE;
}

gboolean
csm_manager_is_session_running (CsmManager *manager,
                                gboolean *running,
                                GError **error)
{
        CsmManagerPrivate *priv;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        priv = csm_manager_get_instance_private (manager);
        *running = (priv->phase == CSM_MANAGER_PHASE_RUNNING);
        return TRUE;
}
