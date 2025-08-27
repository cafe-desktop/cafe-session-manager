/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "csm-marshal.h"
#include "csm-consolekit.h"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

typedef struct
{
        DBusGConnection *dbus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *ck_proxy;
        guint32          is_connected : 1;
} CsmConsolekitPrivate;

enum {
        PROP_0,
        PROP_IS_CONNECTED
};

enum {
        REQUEST_COMPLETED = 0,
        PRIVILEGES_COMPLETED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void     csm_consolekit_finalize     (GObject            *object);

static void     csm_consolekit_free_dbus    (CsmConsolekit      *manager);

static DBusHandlerResult csm_consolekit_dbus_filter (DBusConnection *connection,
                                                     DBusMessage    *message,
                                                     void           *user_data);

static void     csm_consolekit_on_name_owner_changed (DBusGProxy        *bus_proxy,
                                                      const char        *name,
                                                      const char        *prev_owner,
                                                      const char        *new_owner,
                                                      CsmConsolekit   *manager);

G_DEFINE_TYPE_WITH_PRIVATE (CsmConsolekit, csm_consolekit, G_TYPE_OBJECT);

static void
csm_consolekit_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (object);
        CsmConsolekitPrivate *priv;

        priv = csm_consolekit_get_instance_private (manager);

        switch (prop_id) {
        case PROP_IS_CONNECTED:
                g_value_set_boolean (value,
                                     priv->is_connected);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
                                                   prop_id,
                                                   pspec);
        }
}

static void
csm_consolekit_class_init (CsmConsolekitClass *manager_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->finalize = csm_consolekit_finalize;
        object_class->get_property = csm_consolekit_get_property;

        param_spec = g_param_spec_boolean ("is-connected",
                                           "Is connected",
                                           "Whether the session is connected to ConsoleKit",
                                           FALSE,
                                           G_PARAM_READABLE);

        g_object_class_install_property (object_class, PROP_IS_CONNECTED,
                                         param_spec);

        signals [REQUEST_COMPLETED] =
                g_signal_new ("request-completed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmConsolekitClass, request_completed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);

        signals [PRIVILEGES_COMPLETED] =
                g_signal_new ("privileges-completed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmConsolekitClass, privileges_completed),
                              NULL,
                              NULL,
                              csm_marshal_VOID__BOOLEAN_BOOLEAN_POINTER,
                              G_TYPE_NONE,
                              3, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_POINTER);

}

static DBusHandlerResult
csm_consolekit_dbus_filter (DBusConnection *connection G_GNUC_UNUSED,
			    DBusMessage    *message,
			    void           *user_data)
{
        CsmConsolekit *manager;

        manager = CSM_CONSOLEKIT (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                csm_consolekit_free_dbus (manager);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
csm_consolekit_ensure_ck_connection (CsmConsolekit  *manager,
                                     GError        **error)
{
        GError  *connection_error;
        gboolean is_connected;
        CsmConsolekitPrivate *priv;

        connection_error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (priv->dbus_connection == NULL) {
                DBusConnection *connection;

                priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM,
                                                        &connection_error);

                if (priv->dbus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                connection = dbus_g_connection_get_connection (priv->dbus_connection);
                dbus_connection_set_exit_on_disconnect (connection, FALSE);
                dbus_connection_add_filter (connection,
                                            csm_consolekit_dbus_filter,
                                            manager, NULL);
        }

        if (priv->bus_proxy == NULL) {
                priv->bus_proxy =
                        dbus_g_proxy_new_for_name_owner (priv->dbus_connection,
                                                         DBUS_SERVICE_DBUS,
                                                         DBUS_PATH_DBUS,
                                                         DBUS_INTERFACE_DBUS,
                                                         &connection_error);

                if (priv->bus_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                dbus_g_proxy_add_signal (priv->bus_proxy,
                                         "NameOwnerChanged",
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (priv->bus_proxy,
                                             "NameOwnerChanged",
                                             G_CALLBACK (csm_consolekit_on_name_owner_changed),
                                             manager, NULL);
        }

        if (priv->ck_proxy == NULL) {
                priv->ck_proxy =
                        dbus_g_proxy_new_for_name_owner (priv->dbus_connection,
                                                         "org.freedesktop.ConsoleKit",
                                                         "/org/freedesktop/ConsoleKit/Manager",
                                                         "org.freedesktop.ConsoleKit.Manager",
                                                         &connection_error);

                if (priv->ck_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }
        }

        is_connected = TRUE;

out:
        if (priv->is_connected != is_connected) {
                priv->is_connected = is_connected;
                g_object_notify (G_OBJECT (manager), "is-connected");
        }

        if (!is_connected) {
                if (priv->dbus_connection == NULL) {
                        if (priv->bus_proxy != NULL) {
                                g_object_unref (priv->bus_proxy);
                                priv->bus_proxy = NULL;
                        }

                        if (priv->ck_proxy != NULL) {
                                g_object_unref (priv->ck_proxy);
                                priv->ck_proxy = NULL;
                        }
                } else if (priv->bus_proxy == NULL) {
                        if (priv->ck_proxy != NULL) {
                                g_object_unref (priv->ck_proxy);
                                priv->ck_proxy = NULL;
                        }
                }
        }

        return is_connected;
}

static void
csm_consolekit_on_name_owner_changed (DBusGProxy    *bus_proxy G_GNUC_UNUSED,
				      const char    *name,
				      const char    *prev_owner G_GNUC_UNUSED,
				      const char    *new_owner G_GNUC_UNUSED,
				      CsmConsolekit *manager)
{
        CsmConsolekitPrivate *priv;

        if (name != NULL && strcmp (name, "org.freedesktop.ConsoleKit") != 0) {
                return;
        }

        priv = csm_consolekit_get_instance_private (manager);

        if (priv->ck_proxy != NULL) {
                g_object_unref (priv->ck_proxy);
                priv->ck_proxy = NULL;
        }

        csm_consolekit_ensure_ck_connection (manager, NULL);
}

static void
csm_consolekit_init (CsmConsolekit *manager)
{
        GError *error;

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
csm_consolekit_free_dbus (CsmConsolekit *manager)
{
        CsmConsolekitPrivate *priv;

        priv = csm_consolekit_get_instance_private (manager);
        if (priv->bus_proxy != NULL) {
                g_object_unref (priv->bus_proxy);
                priv->bus_proxy = NULL;
        }

        if (priv->ck_proxy != NULL) {
                g_object_unref (priv->ck_proxy);
                priv->ck_proxy = NULL;
        }

        if (priv->dbus_connection != NULL) {
                DBusConnection *connection;
                connection = dbus_g_connection_get_connection (priv->dbus_connection);
                dbus_connection_remove_filter (connection,
                                               csm_consolekit_dbus_filter,
                                               manager);

                dbus_g_connection_unref (priv->dbus_connection);
                priv->dbus_connection = NULL;
        }
}

static void
csm_consolekit_finalize (GObject *object)
{
        CsmConsolekit *manager;
        GObjectClass  *parent_class;

        manager = CSM_CONSOLEKIT (object);

        parent_class = G_OBJECT_CLASS (csm_consolekit_parent_class);

        csm_consolekit_free_dbus (manager);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

GQuark
csm_consolekit_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("csm-consolekit-error");
        }

        return error_quark;
}

CsmConsolekit *
csm_consolekit_new (void)
{
        CsmConsolekit *manager;

        manager = g_object_new (CSM_TYPE_CONSOLEKIT, NULL);

        return manager;
}

static void
emit_restart_complete (CsmConsolekit *manager,
                       GError        *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (CSM_CONSOLEKIT_ERROR,
                                                  CSM_CONSOLEKIT_ERROR_RESTARTING,
                                                  error->message);
        }

        g_signal_emit (G_OBJECT (manager),
                       signals [REQUEST_COMPLETED],
                       0, call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
emit_stop_complete (CsmConsolekit *manager,
                    GError        *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (CSM_CONSOLEKIT_ERROR,
                                                  CSM_CONSOLEKIT_ERROR_STOPPING,
                                                  error->message);
        }

        g_signal_emit (G_OBJECT (manager),
                       signals [REQUEST_COMPLETED],
                       0, call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

void
csm_consolekit_attempt_restart (CsmConsolekit *manager)
{
        gboolean res;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "Restart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to restart system: %s", error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
        } else {
                emit_restart_complete (manager, NULL);
        }
}

void
csm_consolekit_attempt_stop (CsmConsolekit *manager)
{
        gboolean res;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "Stop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to stop system: %s", error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
        } else {
                emit_stop_complete (manager, NULL);
        }
}

void
csm_consolekit_attempt_suspend (CsmConsolekit *manager)
{
        gboolean res;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "Suspend",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_BOOLEAN, TRUE, /* interactive */
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to suspend system: %s", error->message);
                g_error_free (error);
        }
}

void
csm_consolekit_attempt_hibernate (CsmConsolekit *manager)
{
        gboolean res;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "Hibernate",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_BOOLEAN, TRUE, /* interactive */
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);


        if (!res) {
                g_warning ("Unable to hibernate system: %s", error->message);
                g_error_free (error);
        }
}

static gboolean
get_current_session_id (DBusConnection *connection,
                        char          **session_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                CK_MANAGER_PATH,
                                                CK_MANAGER_INTERFACE,
                                                "GetCurrentSession");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (session_id != NULL) {
                *session_id = g_strdup (value);
        }

        ret = TRUE;
out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static gboolean
get_seat_id_for_session (DBusConnection *connection,
                         const char     *session_id,
                         char          **seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "GetSeatId");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine seat: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (seat_id != NULL) {
                *seat_id = g_strdup (value);
        }

        ret = TRUE;
out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static char *
get_current_seat_id (DBusConnection *connection)
{
        gboolean res;
        char    *session_id;
        char    *seat_id;

        session_id = NULL;
        seat_id = NULL;

        res = get_current_session_id (connection, &session_id);
        if (res) {
                res = get_seat_id_for_session (connection, session_id, &seat_id);
        }
        g_free (session_id);

        return seat_id;
}

void
csm_consolekit_set_session_idle (CsmConsolekit *manager,
                                 gboolean       is_idle)
{
        gboolean        res;
        GError         *error;
        char           *session_id;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       dbus_error;
        DBusMessageIter iter;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        session_id = NULL;
        res = get_current_session_id (dbus_g_connection_get_connection (priv->dbus_connection),
                                      &session_id);
        if (!res) {
                goto out;
        }


        g_debug ("Updating ConsoleKit idle status: %d", is_idle);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "SetIdleHint");
        if (message == NULL) {
                g_debug ("Couldn't allocate the D-Bus message");
                return;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &is_idle);

        /* FIXME: use async? */
        dbus_error_init (&dbus_error);
        reply = dbus_connection_send_with_reply_and_block (dbus_g_connection_get_connection (priv->dbus_connection),
                                                           message,
                                                           -1,
                                                           &dbus_error);
        dbus_message_unref (message);

        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (dbus_error_is_set (&dbus_error)) {
                g_debug ("%s raised:\n %s\n\n", dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
        }

out:
        g_free (session_id);
}

static gboolean
seat_can_activate_sessions (DBusConnection *connection,
                            const char     *seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        gboolean        can_activate;

        can_activate = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                seat_id,
                                                CK_SEAT_INTERFACE,
                                                "CanActivateSessions");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to activate session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &can_activate);

out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return can_activate;
}

gboolean
csm_consolekit_can_switch_user (CsmConsolekit *manager)
{
        GError  *error;
        char    *seat_id;
        gboolean ret;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        seat_id = get_current_seat_id (dbus_g_connection_get_connection (priv->dbus_connection));
        if (seat_id == NULL || seat_id[0] == '\0') {
                g_debug ("seat id is not set; can't switch sessions");
                return FALSE;
        }

        ret = seat_can_activate_sessions (dbus_g_connection_get_connection (priv->dbus_connection),
                                          seat_id);
        g_free (seat_id);

        return ret;
}

gboolean
csm_consolekit_get_restart_privileges (CsmConsolekit *manager)
{
        g_signal_emit (G_OBJECT (manager),
                       signals [PRIVILEGES_COMPLETED],
                       0, TRUE, TRUE, NULL);

        return TRUE;
}

gboolean
csm_consolekit_get_stop_privileges (CsmConsolekit *manager)
{
        g_signal_emit (G_OBJECT (manager),
                       signals [PRIVILEGES_COMPLETED],
                       0, TRUE, TRUE, NULL);

        return TRUE;
}

gboolean
csm_consolekit_can_restart (CsmConsolekit *manager)
{
        gboolean res;
        gboolean can_restart;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "CanRestart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_restart,
                                              G_TYPE_INVALID);
        if (res == FALSE) {
                g_warning ("Could not make DBUS call: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        return can_restart;
}

gboolean
csm_consolekit_can_stop (CsmConsolekit *manager)
{
        gboolean res;
        gboolean can_stop;
        GError  *error;
        CsmConsolekitPrivate *priv;

        error = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "CanStop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_stop,
                                              G_TYPE_INVALID);

        if (res == FALSE) {
                g_warning ("Could not make DBUS call: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        return can_stop;
}

gboolean
csm_consolekit_can_suspend (CsmConsolekit *manager)
{
        gboolean res;
        gboolean can_suspend;
        gchar *retval;
        GError *error = NULL;
        CsmConsolekitPrivate *priv;

        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "CanSuspend",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_STRING, &retval,
                                              G_TYPE_INVALID);

        if (res == FALSE) {
                g_warning ("Could not make DBUS call: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        can_suspend = g_strcmp0 (retval, "yes") == 0 ||
                      g_strcmp0 (retval, "challenge") == 0;

        g_free (retval);
        return can_suspend;
}

gboolean
csm_consolekit_can_hibernate (CsmConsolekit *manager)
{
        gboolean res;
        gboolean can_hibernate;
        gchar *retval;
        GError *error = NULL;
        CsmConsolekitPrivate *priv;

        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (priv->ck_proxy,
                                              "CanHibernate",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_STRING, &retval,
                                              G_TYPE_INVALID);

        if (res == FALSE) {
                g_warning ("Could not make DBUS call: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        can_hibernate = g_strcmp0 (retval, "yes") == 0 ||
                        g_strcmp0 (retval, "challenge") == 0;

        g_free (retval);
        return can_hibernate;
}

gchar *
csm_consolekit_get_current_session_type (CsmConsolekit *manager)
{
        GError *gerror;
        DBusConnection *connection;
        DBusError error;
        DBusMessage *message = NULL;
        DBusMessage *reply = NULL;
        gchar *session_id;
        gchar *ret;
        DBusMessageIter iter;
        const char *value;
        CsmConsolekitPrivate *priv;

        session_id = NULL;
        ret = NULL;
        gerror = NULL;
        priv = csm_consolekit_get_instance_private (manager);

        if (!csm_consolekit_ensure_ck_connection (manager, &gerror)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           gerror->message);
                g_error_free (gerror);
                goto out;
        }

        connection = dbus_g_connection_get_connection (priv->dbus_connection);
        if (!get_current_session_id (connection, &session_id)) {
                goto out;
        }

        dbus_error_init (&error);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "GetSessionType");
        if (message == NULL) {
                goto out;
        }

        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &error);

        if (reply == NULL) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("Unable to determine session type: %s", error.message);
                        dbus_error_free (&error);
                }
                goto out;
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        ret = g_strdup (value);

out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }
        g_free (session_id);

        return ret;
}


CsmConsolekit *
csm_get_consolekit (void)
{
        static CsmConsolekit *manager = NULL;

        if (manager == NULL) {
                manager = csm_consolekit_new ();
        }

        return g_object_ref (manager);
}
