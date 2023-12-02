/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#include <glib.h>
#include <glib/gi18n.h>
#include <ctk/ctk.h>

#include "csm-util.h"

#include "csm-app-dialog.h"

#define CTKBUILDER_FILE "session-properties.ui"

#define CAPPLET_NAME_ENTRY_WIDGET_NAME    "session_properties_name_entry"
#define CAPPLET_COMMAND_ENTRY_WIDGET_NAME "session_properties_command_entry"
#define CAPPLET_COMMENT_ENTRY_WIDGET_NAME "session_properties_comment_entry"
#define CAPPLET_DELAY_SPIN_WIDGET_NAME    "session_properties_delay_spin"
#define CAPPLET_BROWSE_WIDGET_NAME        "session_properties_browse_button"

#ifdef __GNUC__
#define UNUSED_VARIABLE __attribute__ ((unused))
#else
#define UNUSED_VARIABLE
#endif

struct _GsmAppDialog
{
        CtkDialog  parent;
        CtkWidget *name_entry;
        CtkWidget *command_entry;
        CtkWidget *comment_entry;
        CtkWidget *delay_spin;
        CtkWidget *browse_button;
        char      *name;
        char      *command;
        char      *comment;
        guint      delay;
};

enum {
        PROP_0,
        PROP_NAME,
        PROP_COMMAND,
        PROP_COMMENT,
        PROP_DELAY
};

G_DEFINE_TYPE (GsmAppDialog, gsm_app_dialog, CTK_TYPE_DIALOG)

static char *
make_exec_uri (const char *exec)
{
        GString    *str;
        const char *c;

        if (exec == NULL) {
                return g_strdup ("");
        }

        if (strchr (exec, ' ') == NULL) {
                return g_strdup (exec);
        }

        str = g_string_new_len (NULL, strlen (exec));

        str = g_string_append_c (str, '"');
        for (c = exec; *c != '\0'; c++) {
                /* FIXME: GKeyFile will add an additional backslach so we'll
                 * end up with toto\\" instead of toto\"
                 * We could use g_key_file_set_value(), but then we don't
                 * benefit from the other escaping that glib is doing...
                 */
                if (*c == '"') {
                        str = g_string_append (str, "\\\"");
                } else {
                        str = g_string_append_c (str, *c);
                }
        }
        str = g_string_append_c (str, '"');

        return g_string_free (str, FALSE);
}

static void
on_browse_button_clicked (CtkWidget    *widget,
                          GsmAppDialog *dialog)
{
        CtkWidget *chooser;
        int        response;

        chooser = ctk_file_chooser_dialog_new ("",
                                               CTK_WINDOW (dialog),
                                               CTK_FILE_CHOOSER_ACTION_OPEN,
                                               "ctk-cancel",
                                               CTK_RESPONSE_CANCEL,
                                               "ctk-open",
                                               CTK_RESPONSE_ACCEPT,
                                               NULL);

        ctk_window_set_transient_for (CTK_WINDOW (chooser),
                                      CTK_WINDOW (dialog));

        ctk_window_set_destroy_with_parent (CTK_WINDOW (chooser), TRUE);

        ctk_window_set_title (CTK_WINDOW (chooser), _("Select Command"));

        ctk_widget_show (chooser);

        response = ctk_dialog_run (CTK_DIALOG (chooser));

        if (response == CTK_RESPONSE_ACCEPT) {
                char *text;
                char *uri;

                text = ctk_file_chooser_get_filename (CTK_FILE_CHOOSER (chooser));

                uri = make_exec_uri (text);

                g_free (text);

                ctk_entry_set_text (CTK_ENTRY (dialog->command_entry), uri);

                g_free (uri);
        }

        ctk_widget_destroy (chooser);
}

static void
on_entry_activate (CtkEntry     *entry,
                   GsmAppDialog *dialog)
{
        ctk_dialog_response (CTK_DIALOG (dialog), CTK_RESPONSE_OK);
}

static gboolean
on_spin_output (CtkSpinButton *spin, GsmAppDialog *dialog)
{
        CtkAdjustment *adjustment;
        gchar *text;
        int value;

        adjustment = ctk_spin_button_get_adjustment (spin);
        value = ctk_adjustment_get_value (adjustment);
        dialog->delay = value;

        if (value == 1)
                text = g_strdup_printf ("%d %s", value, _("second"));
        else if (value > 1)
                text = g_strdup_printf ("%d %s", value, _("seconds"));
        else
                text = g_strdup_printf ("%d", value);

        ctk_entry_set_text (CTK_ENTRY (spin), text);
        g_free (text);

        return TRUE;
}

static void
setup_dialog (GsmAppDialog *dialog)
{
        CtkWidget  *content_area;
        CtkWidget  *widget;
        CtkBuilder *xml;
        GError     *error;

        xml = ctk_builder_new ();
        ctk_builder_set_translation_domain (xml, GETTEXT_PACKAGE);

        error = NULL;
        if (!ctk_builder_add_from_file (xml,
                                        CTKBUILDER_DIR "/" CTKBUILDER_FILE,
                                        &error)) {
                if (error) {
                        g_warning ("Could not load capplet UI file: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Could not load capplet UI file.");
                }
        }

        content_area = ctk_dialog_get_content_area (CTK_DIALOG (dialog));
        widget = CTK_WIDGET (ctk_builder_get_object (xml, "main-table"));
        ctk_container_add (CTK_CONTAINER (content_area), widget);

        ctk_container_set_border_width (CTK_CONTAINER (dialog), 6);
        ctk_window_set_icon_name (CTK_WINDOW (dialog), "cafe-session-properties");

        g_object_set (dialog,
                      "resizable", FALSE,
                      NULL);

        gsm_util_dialog_add_button (CTK_DIALOG (dialog),
                                    _("_Cancel"), "process-stop",
                                    CTK_RESPONSE_CANCEL);

        if (dialog->name == NULL
            && dialog->command == NULL
            && dialog->comment == NULL) {
                ctk_window_set_title (CTK_WINDOW (dialog), _("Add Startup Program"));
                gsm_util_dialog_add_button (CTK_DIALOG (dialog),
                                            _("_Add"), "list-add",
                                            CTK_RESPONSE_OK);
        } else {
                ctk_window_set_title (CTK_WINDOW (dialog), _("Edit Startup Program"));
                gsm_util_dialog_add_button (CTK_DIALOG (dialog),
                                            _("_Save"), "document-save",
                                            CTK_RESPONSE_OK);
        }

        dialog->name_entry = CTK_WIDGET (ctk_builder_get_object (xml, CAPPLET_NAME_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->name_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->name != NULL) {
                ctk_entry_set_text (CTK_ENTRY (dialog->name_entry), dialog->name);
        }

        dialog->browse_button = CTK_WIDGET (ctk_builder_get_object (xml, CAPPLET_BROWSE_WIDGET_NAME));
        g_signal_connect (dialog->browse_button,
                          "clicked",
                          G_CALLBACK (on_browse_button_clicked),
                          dialog);

        dialog->command_entry = CTK_WIDGET (ctk_builder_get_object (xml, CAPPLET_COMMAND_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->command_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->command != NULL) {
                ctk_entry_set_text (CTK_ENTRY (dialog->command_entry), dialog->command);
        }

        dialog->comment_entry = CTK_WIDGET (ctk_builder_get_object (xml, CAPPLET_COMMENT_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->comment_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->comment != NULL) {
                ctk_entry_set_text (CTK_ENTRY (dialog->comment_entry), dialog->comment);
        }

        dialog->delay_spin = CTK_WIDGET(ctk_builder_get_object (xml, CAPPLET_DELAY_SPIN_WIDGET_NAME));
        g_signal_connect (dialog->delay_spin,
                          "output",
                          G_CALLBACK (on_spin_output),
                          dialog);
        if (dialog->delay > 0) {
                CtkAdjustment *adjustment;
                adjustment = ctk_spin_button_get_adjustment (CTK_SPIN_BUTTON(dialog->delay_spin));
                ctk_adjustment_set_value (adjustment, (gdouble) dialog->delay);
        }

        if (xml != NULL) {
                g_object_unref (xml);
        }
}

static GObject *
gsm_app_dialog_constructor (GType                  type,
                            guint                  n_construct_app,
                            GObjectConstructParam *construct_app)
{
        GsmAppDialog *dialog;

        dialog = GSM_APP_DIALOG (G_OBJECT_CLASS (gsm_app_dialog_parent_class)->constructor (type,
                                                                                            n_construct_app,
                                                                                            construct_app));

        setup_dialog (dialog);

        return G_OBJECT (dialog);
}

static void
gsm_app_dialog_dispose (GObject *object)
{
        GsmAppDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_APP_DIALOG (object));

        dialog = GSM_APP_DIALOG (object);

        g_free (dialog->name);
        dialog->name = NULL;
        g_free (dialog->command);
        dialog->command = NULL;
        g_free (dialog->comment);
        dialog->comment = NULL;

        G_OBJECT_CLASS (gsm_app_dialog_parent_class)->dispose (object);
}

static void
gsm_app_dialog_set_name (GsmAppDialog *dialog,
                         const char   *name)
{
        g_return_if_fail (GSM_IS_APP_DIALOG (dialog));

        g_free (dialog->name);

        dialog->name = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "name");
}

static void
gsm_app_dialog_set_command (GsmAppDialog *dialog,
                            const char   *name)
{
        g_return_if_fail (GSM_IS_APP_DIALOG (dialog));

        g_free (dialog->command);

        dialog->command = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "command");
}

static void
gsm_app_dialog_set_comment (GsmAppDialog *dialog,
                            const char   *name)
{
        g_return_if_fail (GSM_IS_APP_DIALOG (dialog));

        g_free (dialog->comment);

        dialog->comment = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "comment");
}

static void
gsm_app_dialog_set_delay (GsmAppDialog *dialog,
                          guint delay)
{
        g_return_if_fail (GSM_IS_APP_DIALOG (dialog));

        dialog->delay = delay;
        g_object_notify (G_OBJECT (dialog), "delay");
}

const char *
gsm_app_dialog_get_name (GsmAppDialog *dialog)
{
        g_return_val_if_fail (GSM_IS_APP_DIALOG (dialog), NULL);
        return ctk_entry_get_text (CTK_ENTRY (dialog->name_entry));
}

const char *
gsm_app_dialog_get_command (GsmAppDialog *dialog)
{
        g_return_val_if_fail (GSM_IS_APP_DIALOG (dialog), NULL);
        return ctk_entry_get_text (CTK_ENTRY (dialog->command_entry));
}

const char *
gsm_app_dialog_get_comment (GsmAppDialog *dialog)
{
        g_return_val_if_fail (GSM_IS_APP_DIALOG (dialog), NULL);
        return ctk_entry_get_text (CTK_ENTRY (dialog->comment_entry));
}

guint
gsm_app_dialog_get_delay (GsmAppDialog *dialog)
{
        g_return_val_if_fail (GSM_IS_APP_DIALOG (dialog), 0);
        return dialog->delay;
}

static void
gsm_app_dialog_set_property (GObject        *object,
                             guint           prop_id,
                             const GValue   *value,
                             GParamSpec     *pspec)
{
        GsmAppDialog *dialog = GSM_APP_DIALOG (object);

        switch (prop_id) {
        case PROP_NAME:
                gsm_app_dialog_set_name (dialog, g_value_get_string (value));
                break;
        case PROP_COMMAND:
                gsm_app_dialog_set_command (dialog, g_value_get_string (value));
                break;
        case PROP_COMMENT:
                gsm_app_dialog_set_comment (dialog, g_value_get_string (value));
                break;
        case PROP_DELAY:
                gsm_app_dialog_set_delay (dialog, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_app_dialog_get_property (GObject        *object,
                             guint           prop_id,
                             GValue         *value,
                             GParamSpec     *pspec)
{
        GsmAppDialog *dialog = GSM_APP_DIALOG (object);

        switch (prop_id) {
        case PROP_NAME:
                g_value_set_string (value, dialog->name);
                break;
        case PROP_COMMAND:
                g_value_set_string (value, dialog->command);
                break;
        case PROP_COMMENT:
                g_value_set_string (value, dialog->comment);
                break;
        case PROP_DELAY:
                g_value_set_uint (value, dialog->delay);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_app_dialog_class_init (GsmAppDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_app_dialog_get_property;
        object_class->set_property = gsm_app_dialog_set_property;
        object_class->constructor = gsm_app_dialog_constructor;
        object_class->dispose = gsm_app_dialog_dispose;

        g_object_class_install_property (object_class,
                                         PROP_NAME,
                                         g_param_spec_string ("name",
                                                              "name",
                                                              "name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COMMAND,
                                         g_param_spec_string ("command",
                                                              "command",
                                                              "command",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COMMENT,
                                         g_param_spec_string ("comment",
                                                              "comment",
                                                              "comment",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_DELAY,
                                         g_param_spec_uint ("delay",
                                                            "delay",
                                                            "delay",
                                                             0,
                                                             100,
                                                             0,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gsm_app_dialog_init (GsmAppDialog *dialog)
{

}

CtkWidget *
gsm_app_dialog_new (const char *name,
                    const char *command,
                    const char *comment,
                    guint delay)
{
        GObject *object;

        object = g_object_new (GSM_TYPE_APP_DIALOG,
                               "name", name,
                               "command", command,
                               "comment", comment,
                               "delay", delay,
                               NULL);

        return CTK_WIDGET (object);
}

gboolean
gsm_app_dialog_run (GsmAppDialog  *dialog,
                    char         **name_p,
                    char         **command_p,
                    char         **comment_p,
                    guint         *delay_p)
{
        gboolean retval;

        retval = FALSE;

        while (ctk_dialog_run (CTK_DIALOG (dialog)) == CTK_RESPONSE_OK) {
                const char *name;
                const char *exec;
                const char *comment;
                guint       delay;
                const char *error_msg;
                GError     *error;
                char      **argv;
                int         argc;

                name = gsm_app_dialog_get_name (GSM_APP_DIALOG (dialog));
                exec = gsm_app_dialog_get_command (GSM_APP_DIALOG (dialog));
                comment = gsm_app_dialog_get_comment (GSM_APP_DIALOG (dialog));
                delay = gsm_app_dialog_get_delay (GSM_APP_DIALOG (dialog));

                error = NULL;
                error_msg = NULL;

                if (gsm_util_text_is_blank (exec)) {
                        error_msg = _("The startup command cannot be empty");
                } else {
                        if (!g_shell_parse_argv (exec, &argc, &argv, &error)) {
                                if (error != NULL) {
                                        error_msg = error->message;
                                } else {
                                        error_msg = _("The startup command is not valid");
                                }
                        }
                }

                if (error_msg != NULL) {
                        CtkWidget *msgbox;

                        msgbox = ctk_message_dialog_new (CTK_WINDOW (dialog),
                                                         CTK_DIALOG_MODAL,
                                                         CTK_MESSAGE_ERROR,
                                                         CTK_BUTTONS_CLOSE,
                                                         "%s", error_msg);

                        if (error != NULL) {
                                g_error_free (error);
                        }

                        ctk_dialog_run (CTK_DIALOG (msgbox));

                        ctk_widget_destroy (msgbox);

                        continue;
                }

                if (gsm_util_text_is_blank (name)) {
                        name = argv[0];
                }

                if (name_p) {
                        *name_p = g_strdup (name);
                }

                g_strfreev (argv);

                if (command_p) {
                        *command_p = g_strdup (exec);
                }

                if (comment_p) {
                        *comment_p = g_strdup (comment);
                }

                if (delay_p) {
                        *delay_p = delay;
                }

                retval = TRUE;
                break;
        }

        ctk_widget_destroy (CTK_WIDGET (dialog));

        return retval;
}
