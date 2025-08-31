/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Vincent Untz
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <config.h>

#include <glib/gi18n.h>
#include <ctk/ctk.h>

#include "csm-logout-dialog.h"
#ifdef HAVE_SYSTEMD
#include "csm-systemd.h"
#endif
#include "csm-consolekit.h"
#include "cdm.h"
#include "csm-util.h"

#define CSM_ICON_LOGOUT   "system-log-out"
#define CSM_ICON_SHUTDOWN "system-shutdown"

#define SESSION_SCHEMA     "org.cafe.session"
#define KEY_LOGOUT_TIMEOUT "logout-timeout"

#define LOCKDOWN_SCHEMA            "org.cafe.lockdown"
#define KEY_USER_SWITCHING_DISABLE "disable-user-switching"

typedef enum {
        CSM_DIALOG_LOGOUT_TYPE_LOGOUT,
        CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN
} CsmDialogLogoutType;

struct _CsmLogoutDialog
{
        CtkMessageDialog     parent;
        CsmDialogLogoutType  type;
#ifdef HAVE_SYSTEMD
        CsmSystemd          *systemd;
#endif
        CsmConsolekit       *consolekit;

        CtkWidget           *progressbar;

        int                  timeout;
        unsigned int         timeout_id;

        unsigned int         default_response;
};

static CsmLogoutDialog *current_dialog = NULL;

static void csm_logout_dialog_set_timeout  (CsmLogoutDialog *logout_dialog);

static void csm_logout_dialog_destroy  (CsmLogoutDialog *logout_dialog,
                                        gpointer         data);

static void csm_logout_dialog_show     (CsmLogoutDialog *logout_dialog,
                                        gpointer         data);

enum {
        PROP_0,
        PROP_MESSAGE_TYPE
};

G_DEFINE_TYPE (CsmLogoutDialog, csm_logout_dialog, CTK_TYPE_MESSAGE_DIALOG);

static void
csm_logout_dialog_set_property (GObject      *object,
				guint         prop_id,
				const GValue *value G_GNUC_UNUSED,
				GParamSpec   *pspec)
{
        switch (prop_id) {
        case PROP_MESSAGE_TYPE:
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_logout_dialog_get_property (GObject     *object,
                                guint        prop_id,
                                GValue      *value,
                                GParamSpec  *pspec)
{
        switch (prop_id) {
        case PROP_MESSAGE_TYPE:
                g_value_set_enum (value, CTK_MESSAGE_WARNING);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_logout_dialog_class_init (CsmLogoutDialogClass *klass)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (klass);

        /* This is a workaround to avoid a stupid crash: libcafeui
         * listens for the "show" signal on all CtkMessageDialog and
         * gets the "message-type" of the dialogs. We will crash when
         * it accesses this property if we don't override it since we
         * didn't define it. */
        gobject_class->set_property = csm_logout_dialog_set_property;
        gobject_class->get_property = csm_logout_dialog_get_property;

        g_object_class_override_property (gobject_class,
                                          PROP_MESSAGE_TYPE,
                                          "message-type");
}

static void
csm_logout_dialog_init (CsmLogoutDialog *logout_dialog)
{
        logout_dialog->timeout_id = 0;
        logout_dialog->timeout = 0;
        logout_dialog->default_response = CTK_RESPONSE_CANCEL;

        CtkStyleContext *context;
        context = ctk_widget_get_style_context (CTK_WIDGET (logout_dialog));
        ctk_style_context_add_class (context, "logout-dialog");

        ctk_window_set_skip_taskbar_hint (CTK_WINDOW (logout_dialog), TRUE);
        ctk_window_set_keep_above (CTK_WINDOW (logout_dialog), TRUE);
        ctk_window_stick (CTK_WINDOW (logout_dialog));
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING())
            logout_dialog->systemd = csm_get_systemd ();
        else
#endif
        logout_dialog->consolekit = csm_get_consolekit ();

        g_signal_connect (logout_dialog,
                          "destroy",
                          G_CALLBACK (csm_logout_dialog_destroy),
                          NULL);

        g_signal_connect (logout_dialog,
                          "show",
                          G_CALLBACK (csm_logout_dialog_show),
                          NULL);
}

static void
csm_logout_dialog_destroy (CsmLogoutDialog *logout_dialog,
			   gpointer         data G_GNUC_UNUSED)
{
        if (logout_dialog->timeout_id != 0) {
                g_source_remove (logout_dialog->timeout_id);
                logout_dialog->timeout_id = 0;
        }
#ifdef HAVE_SYSTEMD
        if (logout_dialog->systemd) {
                g_object_unref (logout_dialog->systemd);
                logout_dialog->systemd = NULL;
        }
#endif

        if (logout_dialog->consolekit) {
                g_object_unref (logout_dialog->consolekit);
                logout_dialog->consolekit = NULL;
        }

        current_dialog = NULL;
}

static gboolean
csm_logout_supports_system_suspend (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;
        ret = FALSE;
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING())
            ret = csm_systemd_can_suspend (logout_dialog->systemd);
        else
#endif
        ret = csm_consolekit_can_suspend (logout_dialog->consolekit);
        return ret;
}

static gboolean
csm_logout_supports_system_hibernate (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;
        ret = FALSE;
#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING())
            ret = csm_systemd_can_hibernate (logout_dialog->systemd);
        else
#endif
        ret = csm_consolekit_can_hibernate (logout_dialog->consolekit);
        return ret;
}

static gboolean
csm_logout_supports_switch_user (CsmLogoutDialog *logout_dialog)
{
        GSettings *settings;
        gboolean   ret = FALSE;
        gboolean   locked;

        settings = g_settings_new (LOCKDOWN_SCHEMA);

        locked = g_settings_get_boolean (settings, KEY_USER_SWITCHING_DISABLE);
        g_object_unref (settings);

        if (!locked) {
#ifdef HAVE_SYSTEMD
            if (LOGIND_RUNNING())
                ret = csm_systemd_can_switch_user (logout_dialog->systemd);
            else
#endif
            ret = csm_consolekit_can_switch_user (logout_dialog->consolekit);
        }

        return ret;
}

static gboolean
csm_logout_supports_reboot (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING())
            ret = csm_systemd_can_restart (logout_dialog->systemd);
        else
#endif
        ret = csm_consolekit_can_restart (logout_dialog->consolekit);
        if (!ret) {
                ret = cdm_supports_logout_action (CDM_LOGOUT_ACTION_REBOOT);
        }

        return ret;
}

static gboolean
csm_logout_supports_shutdown (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;

#ifdef HAVE_SYSTEMD
        if (LOGIND_RUNNING())
            ret = csm_systemd_can_stop (logout_dialog->systemd);
        else
#endif
        ret = csm_consolekit_can_stop (logout_dialog->consolekit);

        if (!ret) {
                ret = cdm_supports_logout_action (CDM_LOGOUT_ACTION_SHUTDOWN);
        }

        return ret;
}

static void
csm_logout_dialog_show (CsmLogoutDialog *logout_dialog,
			gpointer         user_data G_GNUC_UNUSED)
{
        csm_logout_dialog_set_timeout (logout_dialog);
}

static gboolean
csm_logout_dialog_timeout (gpointer data)
{
        CsmLogoutDialog *logout_dialog;
        char            *seconds_warning;
        char            *secondary_text;
        static char     *session_type = NULL;
        static gboolean  is_not_login;

        logout_dialog = (CsmLogoutDialog *) data;

        if (!logout_dialog->timeout) {
                ctk_dialog_response (CTK_DIALOG (logout_dialog),
                                     logout_dialog->default_response);

                return FALSE;
        }

        switch (logout_dialog->type) {
        case CSM_DIALOG_LOGOUT_TYPE_LOGOUT:
                seconds_warning = ngettext ("You will be automatically logged "
                                            "out in %d second",
                                            "You will be automatically logged "
                                            "out in %d seconds",
                                            logout_dialog->timeout);
                break;

        case CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN:
                seconds_warning = ngettext ("This system will be automatically "
                                            "shut down in %d second",
                                            "This system will be automatically "
                                            "shut down in %d seconds",
                                            logout_dialog->timeout);
                break;

        default:
                g_assert_not_reached ();
        }
        seconds_warning = g_strdup_printf (seconds_warning, logout_dialog->timeout);

        if (session_type == NULL) {
#ifdef HAVE_SYSTEMD
                if (LOGIND_RUNNING()) {
                    CsmSystemd *systemd;
                    systemd = csm_get_systemd ();
                    session_type = csm_systemd_get_current_session_type (systemd);
                    g_object_unref (systemd);
                    is_not_login = (g_strcmp0 (session_type, CSM_SYSTEMD_SESSION_TYPE_LOGIN_WINDOW) != 0);
                }
                else {
#endif
                CsmConsolekit *consolekit;
                consolekit = csm_get_consolekit ();
                session_type = csm_consolekit_get_current_session_type (consolekit);
                g_object_unref (consolekit);
                is_not_login = (g_strcmp0 (session_type, CSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) != 0);
#ifdef HAVE_SYSTEMD
                }
#endif
        }

        if (is_not_login) {
                char *name;

                name = g_locale_to_utf8 (g_get_real_name (), -1, NULL, NULL, NULL);

                if (!name || name[0] == '\0' || strcmp (name, "Unknown") == 0) {
                        name = g_locale_to_utf8 (g_get_user_name (), -1 , NULL, NULL, NULL);
                }

                if (!name) {
                        name = g_strdup (g_get_user_name ());
                }

                secondary_text = g_strdup_printf (_("You are currently logged in as \"%s\"."), name);

                g_free (name);
        } else {
                secondary_text = g_strdup (seconds_warning);
        }

        ctk_progress_bar_set_fraction (CTK_PROGRESS_BAR (logout_dialog->progressbar),
                                       logout_dialog->timeout / 60.0);
        ctk_progress_bar_set_text (CTK_PROGRESS_BAR (logout_dialog->progressbar),
                                   seconds_warning);

        ctk_message_dialog_format_secondary_text (CTK_MESSAGE_DIALOG (logout_dialog),
                                                  secondary_text,
                                                  NULL);

        logout_dialog->timeout--;

        g_free (secondary_text);
        g_free (seconds_warning);

        return TRUE;
}

static void
csm_logout_dialog_set_timeout (CsmLogoutDialog *logout_dialog)
{
        GSettings *settings;

        settings = g_settings_new (SESSION_SCHEMA);

        logout_dialog->timeout = g_settings_get_int (settings, KEY_LOGOUT_TIMEOUT);

        if (logout_dialog->timeout > 0) {
                /* Sets the secondary text */
                csm_logout_dialog_timeout (logout_dialog);

                if (logout_dialog->timeout_id != 0) {
                        g_source_remove (logout_dialog->timeout_id);
                }

                logout_dialog->timeout_id = g_timeout_add (1000,
                                                           csm_logout_dialog_timeout,
                                                           logout_dialog);
        }
        else {
                ctk_widget_hide (logout_dialog->progressbar);
        }

        g_object_unref (settings);
}

static CtkWidget *
csm_get_dialog (CsmDialogLogoutType type,
		CdkScreen          *screen,
		guint32             activate_time G_GNUC_UNUSED)
{
        CsmLogoutDialog *logout_dialog;
        CtkWidget       *hbox;
        const char      *primary_text;
        const char      *icon_name;

        if (current_dialog != NULL) {
                ctk_widget_destroy (CTK_WIDGET (current_dialog));
        }

        logout_dialog = g_object_new (CSM_TYPE_LOGOUT_DIALOG, NULL);

        current_dialog = logout_dialog;

        ctk_window_set_title (CTK_WINDOW (logout_dialog), "");

        logout_dialog->type = type;

        icon_name = NULL;
        primary_text = NULL;

        switch (type) {
        case CSM_DIALOG_LOGOUT_TYPE_LOGOUT:
                icon_name    = CSM_ICON_LOGOUT;
                primary_text = _("Log out of this system now?");

                logout_dialog->default_response = CSM_LOGOUT_RESPONSE_LOGOUT;

                if (csm_logout_supports_switch_user (logout_dialog)) {
                        ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                               _("_Switch User"),
                                               CSM_LOGOUT_RESPONSE_SWITCH_USER);
                }

                csm_util_dialog_add_button (CTK_DIALOG (logout_dialog),
                                            _("_Cancel"), "process-stop",
                                            CTK_RESPONSE_CANCEL);

                ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                       _("_Log Out"),
                                       CSM_LOGOUT_RESPONSE_LOGOUT);

                break;
        case CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN:
                icon_name    = CSM_ICON_SHUTDOWN;
                primary_text = _("Shut down this system now?");

                logout_dialog->default_response = CSM_LOGOUT_RESPONSE_SHUTDOWN;

                if (csm_logout_supports_system_suspend (logout_dialog)) {
                        ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                               _("S_uspend"),
                                               CSM_LOGOUT_RESPONSE_SLEEP);
                }

                if (csm_logout_supports_system_hibernate (logout_dialog)) {
                        ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                               _("_Hibernate"),
                                               CSM_LOGOUT_RESPONSE_HIBERNATE);
                }

                if (csm_logout_supports_reboot (logout_dialog)) {
                        ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                               _("_Restart"),
                                               CSM_LOGOUT_RESPONSE_REBOOT);
                }

                csm_util_dialog_add_button (CTK_DIALOG (logout_dialog),
                                            _("_Cancel"), "process-stop",
                                            CTK_RESPONSE_CANCEL);

                if (csm_logout_supports_shutdown (logout_dialog)) {
                        ctk_dialog_add_button (CTK_DIALOG (logout_dialog),
                                               _("_Shut Down"),
                                               CSM_LOGOUT_RESPONSE_SHUTDOWN);
                }
                break;
        default:
                g_assert_not_reached ();
        }

        hbox = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 0);
        logout_dialog->progressbar = ctk_progress_bar_new ();
        ctk_progress_bar_set_show_text (CTK_PROGRESS_BAR (logout_dialog->progressbar), TRUE);
        ctk_progress_bar_set_fraction (CTK_PROGRESS_BAR (logout_dialog->progressbar), 1.0);
        ctk_box_pack_start (CTK_BOX (hbox),
                            logout_dialog->progressbar,
                            TRUE, TRUE, 12);
        ctk_widget_show_all (hbox);
        ctk_container_add (CTK_CONTAINER (ctk_dialog_get_content_area (CTK_DIALOG (logout_dialog))), hbox);

        ctk_window_set_icon_name (CTK_WINDOW (logout_dialog), icon_name);
        ctk_window_set_position (CTK_WINDOW (logout_dialog), CTK_WIN_POS_CENTER_ALWAYS);
        ctk_message_dialog_set_markup (CTK_MESSAGE_DIALOG (logout_dialog), primary_text);

        ctk_dialog_set_default_response (CTK_DIALOG (logout_dialog),
                                         logout_dialog->default_response);

        ctk_window_set_screen (CTK_WINDOW (logout_dialog), screen);

        return CTK_WIDGET (logout_dialog);
}

CtkWidget *
csm_get_shutdown_dialog (CdkScreen *screen,
                         guint32    activate_time)
{
        return csm_get_dialog (CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN,
                               screen,
                               activate_time);
}

CtkWidget *
csm_get_logout_dialog (CdkScreen *screen,
                       guint32    activate_time)
{
        return csm_get_dialog (CSM_DIALOG_LOGOUT_TYPE_LOGOUT,
                               screen,
                               activate_time);
}
