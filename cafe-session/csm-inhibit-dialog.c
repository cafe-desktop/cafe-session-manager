/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <mccann@jhu.edu>
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
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <ctk/ctk.h>
#include <cdk/cdkx.h>
#include <cairo-xlib.h>

#include "csm-inhibit-dialog.h"
#include "csm-store.h"
#include "csm-client.h"
#include "csm-inhibitor.h"
#include "eggdesktopfile.h"
#include "csm-util.h"

#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif

#define CTKBUILDER_FILE "csm-inhibit-dialog.ui"

#ifndef DEFAULT_ICON_SIZE
#define DEFAULT_ICON_SIZE 32
#endif

#ifndef DEFAULT_SNAPSHOT_SIZE
#define DEFAULT_SNAPSHOT_SIZE 128
#endif

#define DIALOG_RESPONSE_LOCK_SCREEN 1

struct _CsmInhibitDialog
{
        CtkDialog          parent;
        CtkBuilder        *xml;
        int                action;
        gboolean           is_done;
        CsmStore          *inhibitors;
        CsmStore          *clients;
        CtkListStore      *list_store;
        gboolean           have_xrender;
        int                xrender_event_base;
        int                xrender_error_base;
};

enum {
        PROP_0,
        PROP_ACTION,
        PROP_INHIBITOR_STORE,
        PROP_CLIENT_STORE
};

enum {
        INHIBIT_IMAGE_COLUMN = 0,
        INHIBIT_NAME_COLUMN,
        INHIBIT_REASON_COLUMN,
        INHIBIT_ID_COLUMN,
        NUMBER_OF_COLUMNS
};

static void     csm_inhibit_dialog_finalize    (GObject               *object);

G_DEFINE_TYPE (CsmInhibitDialog, csm_inhibit_dialog, CTK_TYPE_DIALOG)

static void
lock_screen (CsmInhibitDialog *dialog)
{
        GError *error;
        error = NULL;
        g_spawn_command_line_async ("cafe-screensaver-command --lock", &error);
        if (error != NULL) {
                g_warning ("Couldn't lock screen: %s", error->message);
                g_error_free (error);
        }
}

static void
on_response (CsmInhibitDialog *dialog,
             gint              response_id)

{
        if (dialog->is_done) {
                g_signal_stop_emission_by_name (dialog, "response");
                return;
        }

        switch (response_id) {
        case DIALOG_RESPONSE_LOCK_SCREEN:
                g_signal_stop_emission_by_name (dialog, "response");
                lock_screen (dialog);
                break;
        default:
                dialog->is_done = TRUE;
                break;
        }
}

static void
csm_inhibit_dialog_set_action (CsmInhibitDialog *dialog,
                               int               action)
{
        dialog->action = action;
}

static gboolean
find_inhibitor (CsmInhibitDialog *dialog,
                const char       *id,
                CtkTreeIter      *iter)
{
        CtkTreeModel *model;
        gboolean      found_item;

        g_assert (CSM_IS_INHIBIT_DIALOG (dialog));

        found_item = FALSE;
        model = CTK_TREE_MODEL (dialog->list_store);

        if (!ctk_tree_model_get_iter_first (model, iter)) {
                return FALSE;
        }

        do {
                char *item_id;

                ctk_tree_model_get (model,
                                    iter,
                                    INHIBIT_ID_COLUMN, &item_id,
                                    -1);
                if (item_id != NULL
                    && id != NULL
                    && strcmp (item_id, id) == 0) {
                        found_item = TRUE;
                }
                g_free (item_id);
        } while (!found_item && ctk_tree_model_iter_next (model, iter));

        return found_item;
}

/* copied from cafe-panel panel-util.c */
static char *
_util_icon_remove_extension (const char *icon)
{
        char *icon_no_extension;
        char *p;

        icon_no_extension = g_strdup (icon);
        p = strrchr (icon_no_extension, '.');
        if (p &&
            (strcmp (p, ".png") == 0 ||
             strcmp (p, ".xpm") == 0 ||
             strcmp (p, ".svg") == 0)) {
            *p = 0;
        }

        return icon_no_extension;
}

/* copied from cafe-panel panel-util.c */
static char *
_find_icon (CtkIconTheme  *icon_theme,
            const char    *icon_name,
            gint           size)
{
        CtkIconInfo *info;
        char        *retval;
        char        *icon_no_extension;

        if (icon_name == NULL || strcmp (icon_name, "") == 0)
                return NULL;

        if (g_path_is_absolute (icon_name)) {
                if (g_file_test (icon_name, G_FILE_TEST_EXISTS)) {
                        return g_strdup (icon_name);
                } else {
                        char *basename;

                        basename = g_path_get_basename (icon_name);
                        retval = _find_icon (icon_theme, basename,
                                             size);
                        g_free (basename);

                        return retval;
                }
        }

        /* This is needed because some .desktop files have an icon name *and*
         * an extension as icon */
        icon_no_extension = _util_icon_remove_extension (icon_name);

        info = ctk_icon_theme_lookup_icon (icon_theme, icon_no_extension,
                                           size, 0);

        g_free (icon_no_extension);

        if (info) {
                retval = g_strdup (ctk_icon_info_get_filename (info));
                g_object_unref (info);
        } else
                retval = NULL;

        return retval;
}

/* copied from cafe-panel panel-util.c */
static GdkPixbuf *
_load_icon (CtkIconTheme  *icon_theme,
            const char    *icon_name,
            int            size,
            int            desired_width,
            int            desired_height,
            char         **error_msg)
{
        GdkPixbuf *retval;
        char      *file;
        GError    *error;

        g_return_val_if_fail (error_msg == NULL || *error_msg == NULL, NULL);

        file = _find_icon (icon_theme, icon_name, size);
        if (!file) {
                if (error_msg)
                        *error_msg = g_strdup_printf (_("Icon '%s' not found"),
                                                      icon_name);

                return NULL;
        }

        error = NULL;
        retval = gdk_pixbuf_new_from_file_at_size (file,
                                                   desired_width,
                                                   desired_height,
                                                   &error);
        if (error) {
                if (error_msg)
                        *error_msg = g_strdup (error->message);
                g_error_free (error);
        }

        g_free (file);

        return retval;
}


static GdkPixbuf *
scale_pixbuf (GdkPixbuf *pixbuf,
              int        max_width,
              int        max_height,
              gboolean   no_stretch_hint)
{
        int        pw;
        int        ph;
        float      scale_factor_x = 1.0;
        float      scale_factor_y = 1.0;
        float      scale_factor = 1.0;

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        /* Determine which dimension requires the smallest scale. */
        scale_factor_x = (float) max_width / (float) pw;
        scale_factor_y = (float) max_height / (float) ph;

        if (scale_factor_x > scale_factor_y) {
                scale_factor = scale_factor_y;
        } else {
                scale_factor = scale_factor_x;
        }

        /* always scale down, allow to disable scaling up */
        if (scale_factor < 1.0 || !no_stretch_hint) {
                int scale_x = (int) (pw * scale_factor);
                int scale_y = (int) (ph * scale_factor);
                g_debug ("Scaling to %dx%d", scale_x, scale_y);
                return gdk_pixbuf_scale_simple (pixbuf,
                                                scale_x,
                                                scale_y,
                                                GDK_INTERP_BILINEAR);
        } else {
                return g_object_ref (pixbuf);
        }
}

#ifdef HAVE_XRENDER
static GdkPixbuf *
pixbuf_get_from_pixmap (Display *display,
                        Pixmap xpixmap,
                        int width,
                        int height)
{
        GdkPixbuf   *retval;
        cairo_surface_t *surface;
        Visual *visual;
        retval = NULL;

        g_debug ("CsmInhibitDialog: getting foreign pixmap for %u", (guint)xpixmap);

        visual = DefaultVisual (display, 0);
        surface = cairo_xlib_surface_create (display,
                                             xpixmap,
                                             visual,
                                             width,
                                             height);
        if (surface != NULL) {
                g_debug ("CsmInhibitDialog: getting pixbuf w=%d h=%d", width, height);
                retval = gdk_pixbuf_get_from_surface (surface,
                                                      0, 0,
                                                      width, height);
                cairo_surface_destroy (surface);
        }

        return retval;
}

static Pixmap
get_pixmap_for_window (Display *display,
                       Window window,
                       int *widthp,
                       int *heightp)
{
        XWindowAttributes        attr;
        XRenderPictureAttributes pa;
        Pixmap                   pixmap;
        XRenderPictFormat       *format;
        Picture                  src_picture;
        Picture                  dst_picture;
        gboolean                 has_alpha;
        int                      width;
        int                      height;

        XGetWindowAttributes (display, window, &attr);

        format = XRenderFindVisualFormat (display, attr.visual);
        has_alpha = (format->type == PictTypeDirect && format->direct.alphaMask);
        width = attr.width;
        height = attr.height;

        pa.subwindow_mode = IncludeInferiors; /* Don't clip child widgets */

        src_picture = XRenderCreatePicture (display, window, format, CPSubwindowMode, &pa);

        pixmap = XCreatePixmap (display,
                                window,
                                width, height,
                                attr.depth);

        dst_picture = XRenderCreatePicture (display, pixmap, format, 0, 0);
        XRenderComposite (display,
                          has_alpha ? PictOpOver : PictOpSrc,
                          src_picture,
                          None,
                          dst_picture,
                          0, 0, 0, 0,
                          0, 0,
                          width, height);

        if (widthp != NULL) {
                *widthp = width;
        }
        if (heightp != NULL) {
                *heightp = height;
        }

        return pixmap;
}

#endif /* HAVE_COMPOSITE */

static GdkPixbuf *
get_pixbuf_for_window (CdkDisplay *cdkdisplay,
                       guint xid,
                       int thumb_width,
                       int thumb_height)
{
        GdkPixbuf *pixbuf = NULL;
#ifdef HAVE_XRENDER
        Display   *display;
        Window     xwindow;
        Pixmap     xpixmap;
        int width;
        int height;

        display = CDK_DISPLAY_XDISPLAY (cdkdisplay);
        xwindow = (Window) xid;
        xpixmap = get_pixmap_for_window (display, xwindow, &width, &height);
        if (xpixmap == None) {
                g_debug ("CsmInhibitDialog: Unable to get window snapshot for %u", xid);
                return NULL;
        } else {
                g_debug ("CsmInhibitDialog: Got xpixmap %u", (guint)xpixmap);
        }

        pixbuf = pixbuf_get_from_pixmap (display, xpixmap, width, height);

        if (xpixmap != None) {
                cdk_x11_display_error_trap_push (cdkdisplay);
                XFreePixmap (display, xpixmap);
                cdk_display_sync (cdkdisplay);
                cdk_x11_display_error_trap_pop_ignored (cdkdisplay);
        }

        if (pixbuf != NULL) {
                GdkPixbuf *scaled;
                g_debug ("CsmInhibitDialog: scaling pixbuf to w=%d h=%d", width, height);
                scaled = scale_pixbuf (pixbuf, thumb_width, thumb_height, TRUE);
                g_object_unref (pixbuf);
                pixbuf = scaled;
        }
#else
        g_debug ("CsmInhibitDialog: no support for getting window snapshot");
#endif
        return pixbuf;
}

static void
add_inhibitor (CsmInhibitDialog *dialog,
               CsmInhibitor     *inhibitor)
{
        CdkDisplay     *cdkdisplay;
        const char     *name;
        const char     *icon_name;
        const char     *app_id;
        char           *desktop_filename;
        GdkPixbuf      *pixbuf;
        EggDesktopFile *desktop_file;
        GError         *error;
        char          **search_dirs;
        guint           xid;
        char           *freeme;

        cdkdisplay = ctk_widget_get_display (CTK_WIDGET (dialog));

        /* FIXME: get info from xid */

        desktop_file = NULL;
        name = NULL;
        pixbuf = NULL;
        freeme = NULL;

        app_id = csm_inhibitor_peek_app_id (inhibitor);

        if (IS_STRING_EMPTY (app_id)) {
                desktop_filename = NULL;
        } else if (! g_str_has_suffix (app_id, ".desktop")) {
                desktop_filename = g_strdup_printf ("%s.desktop", app_id);
        } else {
                desktop_filename = g_strdup (app_id);
        }

        xid = csm_inhibitor_peek_toplevel_xid (inhibitor);
        g_debug ("CsmInhibitDialog: inhibitor has XID %u", xid);
        if (xid > 0 && dialog->have_xrender) {
                pixbuf = get_pixbuf_for_window (cdkdisplay, xid, DEFAULT_SNAPSHOT_SIZE, DEFAULT_SNAPSHOT_SIZE);
                if (pixbuf == NULL) {
                        g_debug ("CsmInhibitDialog: unable to read pixbuf from %u", xid);
                }
        }

        if (desktop_filename != NULL) {
                search_dirs = csm_util_get_desktop_dirs ();

                if (g_path_is_absolute (desktop_filename)) {
                        char *basename;

                        error = NULL;
                        desktop_file = egg_desktop_file_new (desktop_filename,
                                                             &error);
                        if (desktop_file == NULL) {
                                if (error) {
                                        g_warning ("Unable to load desktop file '%s': %s",
                                                   desktop_filename, error->message);
                                        g_error_free (error);
                                } else {
                                        g_warning ("Unable to load desktop file '%s'",
                                                   desktop_filename);
                                }

                                basename = g_path_get_basename (desktop_filename);
                                g_free (desktop_filename);
                                desktop_filename = basename;
                        }
                }

                if (desktop_file == NULL) {
                        error = NULL;
                        desktop_file = egg_desktop_file_new_from_dirs (desktop_filename,
                                                                       (const char **)search_dirs,
                                                                       &error);
                }

                /* look for a file with a vendor prefix */
                if (desktop_file == NULL) {
                        if (error) {
                                g_warning ("Unable to find desktop file '%s': %s",
                                           desktop_filename, error->message);
                                g_error_free (error);
                        } else {
                                g_warning ("Unable to find desktop file '%s'",
                                           desktop_filename);
                        }
                        g_free (desktop_filename);
                        desktop_filename = g_strdup_printf ("cafe-%s.desktop", app_id);
                        error = NULL;
                        desktop_file = egg_desktop_file_new_from_dirs (desktop_filename,
                                                                       (const char **)search_dirs,
                                                                       &error);
                }
                g_strfreev (search_dirs);

                if (desktop_file == NULL) {
                        if (error) {
                                g_warning ("Unable to find desktop file '%s': %s",
                                           desktop_filename, error->message);
                                g_error_free (error);
                        } else {
                                g_warning ("Unable to find desktop file '%s'",
                                           desktop_filename);
                        }
                } else {
                        name = egg_desktop_file_get_name (desktop_file);
                        icon_name = egg_desktop_file_get_icon (desktop_file);

                        if (pixbuf == NULL) {
                                pixbuf = _load_icon (ctk_icon_theme_get_default (),
                                                     icon_name,
                                                     DEFAULT_ICON_SIZE,
                                                     DEFAULT_ICON_SIZE,
                                                     DEFAULT_ICON_SIZE,
                                                     NULL);
                        }
                }
        }

        /* try client info */
        if (name == NULL) {
                const char *client_id;
                client_id = csm_inhibitor_peek_client_id (inhibitor);
                if (! IS_STRING_EMPTY (client_id)) {
                        CsmClient *client;
                        client = CSM_CLIENT (csm_store_lookup (dialog->clients, client_id));
                        if (client != NULL) {
                                freeme = csm_client_get_app_name (client);
                                name = freeme;
                        }
                }
        }

        if (name == NULL) {
                if (! IS_STRING_EMPTY (app_id)) {
                        name = app_id;
                } else {
                        name = _("Unknown");
                }
        }

        if (pixbuf == NULL) {
                pixbuf = _load_icon (ctk_icon_theme_get_default (),
                                     "cafe-windows",
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     DEFAULT_ICON_SIZE,
                                     NULL);
        }

        ctk_list_store_insert_with_values (dialog->list_store,
                                           NULL, 0,
                                           INHIBIT_IMAGE_COLUMN, pixbuf,
                                           INHIBIT_NAME_COLUMN, name,
                                           INHIBIT_REASON_COLUMN, csm_inhibitor_peek_reason (inhibitor),
                                           INHIBIT_ID_COLUMN, csm_inhibitor_peek_id (inhibitor),
                                           -1);

        g_free (desktop_filename);
        g_free (freeme);
        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }
        if (desktop_file != NULL) {
                egg_desktop_file_free (desktop_file);
        }
}

static gboolean
model_has_one_entry (CtkTreeModel *model)
{
        guint n_rows;

        n_rows = ctk_tree_model_iter_n_children (model, NULL);
        g_debug ("Model has %d rows", n_rows);

        return (n_rows > 0 && n_rows < 2);
}

static void
update_dialog_text (CsmInhibitDialog *dialog)
{
        const char *description_text;
        const char *header_text;
        CtkWidget  *widget;

        if (model_has_one_entry (CTK_TREE_MODEL (dialog->list_store))) {
                g_debug ("Found one entry in model");
                header_text = _("A program is still running:");
                description_text = _("Waiting for the program to finish.  Interrupting the program may cause you to lose work.");
        } else {
                g_debug ("Found multiple entries (or none) in model");
                header_text = _("Some programs are still running:");
                description_text = _("Waiting for programs to finish.  Interrupting these programs may cause you to lose work.");
        }

        widget = CTK_WIDGET (ctk_builder_get_object (dialog->xml,
                                                     "header-label"));
        if (widget != NULL) {
                char *markup;
                markup = g_strdup_printf ("<b>%s</b>", header_text);
                ctk_label_set_markup (CTK_LABEL (widget), markup);
                g_free (markup);
        }

        widget = CTK_WIDGET (ctk_builder_get_object (dialog->xml,
                                                     "description-label"));
        if (widget != NULL) {
                ctk_label_set_text (CTK_LABEL (widget), description_text);
        }
}

static void
on_store_inhibitor_added (CsmStore          *store,
                          const char        *id,
                          CsmInhibitDialog  *dialog)
{
        CsmInhibitor *inhibitor;
        CtkTreeIter   iter;

        g_debug ("CsmInhibitDialog: inhibitor added: %s", id);

        if (dialog->is_done) {
                return;
        }

        inhibitor = (CsmInhibitor *)csm_store_lookup (store, id);

        /* Add to model */
        if (! find_inhibitor (dialog, id, &iter)) {
                add_inhibitor (dialog, inhibitor);
                update_dialog_text (dialog);
        }

}

static void
on_store_inhibitor_removed (CsmStore          *store,
                            const char        *id,
                            CsmInhibitDialog  *dialog)
{
        CtkTreeIter   iter;

        g_debug ("CsmInhibitDialog: inhibitor removed: %s", id);

        if (dialog->is_done) {
                return;
        }

        /* Remove from model */
        if (find_inhibitor (dialog, id, &iter)) {
                ctk_list_store_remove (dialog->list_store, &iter);
                update_dialog_text (dialog);
        }

        /* if there are no inhibitors left then trigger response */
        if (! ctk_tree_model_get_iter_first (CTK_TREE_MODEL (dialog->list_store), &iter)) {
                ctk_dialog_response (CTK_DIALOG (dialog), CTK_RESPONSE_ACCEPT);
        }
}

static void
csm_inhibit_dialog_set_inhibitor_store (CsmInhibitDialog *dialog,
                                        CsmStore         *store)
{
        g_return_if_fail (CSM_IS_INHIBIT_DIALOG (dialog));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (dialog->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (dialog->inhibitors,
                                                      on_store_inhibitor_added,
                                                      dialog);
                g_signal_handlers_disconnect_by_func (dialog->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      dialog);

                g_object_unref (dialog->inhibitors);
        }


        g_debug ("CsmInhibitDialog: setting store %p", store);

        dialog->inhibitors = store;

        if (dialog->inhibitors != NULL) {
                g_signal_connect (dialog->inhibitors,
                                  "added",
                                  G_CALLBACK (on_store_inhibitor_added),
                                  dialog);
                g_signal_connect (dialog->inhibitors,
                                  "removed",
                                  G_CALLBACK (on_store_inhibitor_removed),
                                  dialog);
        }
}

static void
csm_inhibit_dialog_set_client_store (CsmInhibitDialog *dialog,
                                     CsmStore         *store)
{
        g_return_if_fail (CSM_IS_INHIBIT_DIALOG (dialog));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (dialog->clients != NULL) {
                g_object_unref (dialog->clients);
        }

        dialog->clients = store;
}

static void
csm_inhibit_dialog_set_property (GObject        *object,
                                 guint           prop_id,
                                 const GValue   *value,
                                 GParamSpec     *pspec)
{
        CsmInhibitDialog *dialog = CSM_INHIBIT_DIALOG (object);

        switch (prop_id) {
        case PROP_ACTION:
                csm_inhibit_dialog_set_action (dialog, g_value_get_int (value));
                break;
        case PROP_INHIBITOR_STORE:
                csm_inhibit_dialog_set_inhibitor_store (dialog, g_value_get_object (value));
                break;
        case PROP_CLIENT_STORE:
                csm_inhibit_dialog_set_client_store (dialog, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_inhibit_dialog_get_property (GObject        *object,
                                 guint           prop_id,
                                 GValue         *value,
                                 GParamSpec     *pspec)
{
        CsmInhibitDialog *dialog = CSM_INHIBIT_DIALOG (object);

        switch (prop_id) {
        case PROP_ACTION:
                g_value_set_int (value, dialog->action);
                break;
        case PROP_INHIBITOR_STORE:
                g_value_set_object (value, dialog->inhibitors);
                break;
        case PROP_CLIENT_STORE:
                g_value_set_object (value, dialog->clients);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
name_cell_data_func (CtkTreeViewColumn *tree_column,
                     CtkCellRenderer   *cell,
                     CtkTreeModel      *model,
                     CtkTreeIter       *iter,
                     CsmInhibitDialog  *dialog)
{
        char    *name;
        char    *reason;
        char    *markup;

        name = NULL;
        reason = NULL;
        ctk_tree_model_get (model,
                            iter,
                            INHIBIT_NAME_COLUMN, &name,
                            INHIBIT_REASON_COLUMN, &reason,
                            -1);

        markup = g_strdup_printf ("<b>%s</b>\n"
                                  "<span size=\"small\">%s</span>",
                                  name ? name : "(null)",
                                  reason ? reason : "(null)");

        g_free (name);
        g_free (reason);

        g_object_set (cell, "markup", markup, NULL);
        g_free (markup);
}

static gboolean
add_to_model (const char       *id,
              CsmInhibitor     *inhibitor,
              CsmInhibitDialog *dialog)
{
        add_inhibitor (dialog, inhibitor);
        return FALSE;
}

static void
populate_model (CsmInhibitDialog *dialog)
{
        csm_store_foreach_remove (dialog->inhibitors,
                                  (CsmStoreFunc)add_to_model,
                                  dialog);
        update_dialog_text (dialog);
}

static void
setup_dialog (CsmInhibitDialog *dialog)
{
        const char        *button_text;
        CtkWidget         *treeview;
        CtkTreeViewColumn *column;
        CtkCellRenderer   *renderer;

        switch (dialog->action) {
        case CSM_LOGOUT_ACTION_SWITCH_USER:
                button_text = _("Switch User Anyway");
                break;
        case CSM_LOGOUT_ACTION_LOGOUT:
                button_text = _("Log Out Anyway");
                break;
        case CSM_LOGOUT_ACTION_SLEEP:
                button_text = _("Suspend Anyway");
                break;
        case CSM_LOGOUT_ACTION_HIBERNATE:
                button_text = _("Hibernate Anyway");
                break;
        case CSM_LOGOUT_ACTION_SHUTDOWN:
                button_text = _("Shut Down Anyway");
                break;
        case CSM_LOGOUT_ACTION_REBOOT:
                button_text = _("Reboot Anyway");
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        ctk_dialog_add_button (CTK_DIALOG (dialog),
                               _("Lock Screen"),
                               DIALOG_RESPONSE_LOCK_SCREEN);
        ctk_dialog_add_button (CTK_DIALOG (dialog),
                               _("Cancel"),
                               CTK_RESPONSE_CANCEL);
        ctk_dialog_add_button (CTK_DIALOG (dialog),
                               button_text,
                               CTK_RESPONSE_ACCEPT);
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (on_response),
                          dialog);

        dialog->list_store = ctk_list_store_new (NUMBER_OF_COLUMNS,
                                                 GDK_TYPE_PIXBUF,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING);

        treeview = CTK_WIDGET (ctk_builder_get_object (dialog->xml,
                                                       "inhibitors-treeview"));
        ctk_tree_view_set_headers_visible (CTK_TREE_VIEW (treeview), FALSE);
        ctk_tree_view_set_model (CTK_TREE_VIEW (treeview),
                                 CTK_TREE_MODEL (dialog->list_store));

        /* IMAGE COLUMN */
        renderer = ctk_cell_renderer_pixbuf_new ();
        column = ctk_tree_view_column_new ();
        ctk_tree_view_column_pack_start (column, renderer, FALSE);
        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);

        ctk_tree_view_column_set_attributes (column,
                                             renderer,
                                             "pixbuf", INHIBIT_IMAGE_COLUMN,
                                             NULL);

        g_object_set (renderer, "xalign", 1.0, NULL);

        /* NAME COLUMN */
        renderer = ctk_cell_renderer_text_new ();
        column = ctk_tree_view_column_new ();
        ctk_tree_view_column_pack_start (column, renderer, FALSE);
        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);
        ctk_tree_view_column_set_cell_data_func (column,
                                                 renderer,
                                                 (CtkTreeCellDataFunc) name_cell_data_func,
                                                 dialog,
                                                 NULL);

        ctk_tree_view_set_tooltip_column (CTK_TREE_VIEW (treeview),
                                          INHIBIT_REASON_COLUMN);

        populate_model (dialog);
}

static GObject *
csm_inhibit_dialog_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        CsmInhibitDialog *dialog;
#ifdef HAVE_XRENDER
        CdkDisplay *cdkdisplay;
#endif /* HAVE_XRENDER */
        dialog = CSM_INHIBIT_DIALOG (G_OBJECT_CLASS (csm_inhibit_dialog_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

#ifdef HAVE_XRENDER
        cdkdisplay = cdk_display_get_default ();
        cdk_x11_display_error_trap_push (cdkdisplay);
        if (XRenderQueryExtension (CDK_DISPLAY_XDISPLAY (cdkdisplay), &dialog->xrender_event_base, &dialog->xrender_error_base)) {
                g_debug ("CsmInhibitDialog: Initialized XRender extension");
                dialog->have_xrender = TRUE;
        } else {
                g_debug ("CsmInhibitDialog: Unable to initialize XRender extension");
                dialog->have_xrender = FALSE;
        }
        cdk_display_sync (cdkdisplay);
        cdk_x11_display_error_trap_pop_ignored (cdkdisplay);
#endif /* HAVE_XRENDER */

        /* FIXME: turn this on when it is ready */
        dialog->have_xrender = FALSE;

        setup_dialog (dialog);

        ctk_widget_show_all (CTK_WIDGET (dialog));

        return G_OBJECT (dialog);
}

static void
csm_inhibit_dialog_dispose (GObject *object)
{
        CsmInhibitDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_INHIBIT_DIALOG (object));

        dialog = CSM_INHIBIT_DIALOG (object);

        g_debug ("CsmInhibitDialog: dispose called");

        if (dialog->list_store != NULL) {
                g_object_unref (dialog->list_store);
                dialog->list_store = NULL;
        }

        if (dialog->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (dialog->inhibitors,
                                                      on_store_inhibitor_added,
                                                      dialog);
                g_signal_handlers_disconnect_by_func (dialog->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      dialog);

                g_object_unref (dialog->inhibitors);
                dialog->inhibitors = NULL;
        }

        if (dialog->xml != NULL) {
                g_object_unref (dialog->xml);
                dialog->xml = NULL;
        }

        G_OBJECT_CLASS (csm_inhibit_dialog_parent_class)->dispose (object);
}

static void
csm_inhibit_dialog_class_init (CsmInhibitDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = csm_inhibit_dialog_get_property;
        object_class->set_property = csm_inhibit_dialog_set_property;
        object_class->constructor = csm_inhibit_dialog_constructor;
        object_class->dispose = csm_inhibit_dialog_dispose;
        object_class->finalize = csm_inhibit_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_ACTION,
                                         g_param_spec_int ("action",
                                                           "action",
                                                           "action",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_INHIBITOR_STORE,
                                         g_param_spec_object ("inhibitor-store",
                                                              NULL,
                                                              NULL,
                                                              CSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_STORE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              CSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
csm_inhibit_dialog_init (CsmInhibitDialog *dialog)
{
        CtkWidget *content_area;
        CtkWidget *widget;
        GError    *error;

        dialog->xml = ctk_builder_new ();
        ctk_builder_set_translation_domain (dialog->xml, GETTEXT_PACKAGE);

        error = NULL;
        if (!ctk_builder_add_from_file (dialog->xml,
                                        CTKBUILDER_DIR "/" CTKBUILDER_FILE,
                                        &error)) {
                if (error) {
                        g_warning ("Could not load inhibitor UI file: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Could not load inhibitor UI file.");
                }
        }

        content_area = ctk_dialog_get_content_area (CTK_DIALOG (dialog));
        widget = CTK_WIDGET (ctk_builder_get_object (dialog->xml,
                                                     "main-box"));
        ctk_container_add (CTK_CONTAINER (content_area), widget);

        ctk_container_set_border_width (CTK_CONTAINER (dialog), 6);
        ctk_window_set_icon_name (CTK_WINDOW (dialog), "system-log-out");
        ctk_window_set_title (CTK_WINDOW (dialog), "");
        g_object_set (dialog,
                      "resizable", FALSE,
                      NULL);
}

static void
csm_inhibit_dialog_finalize (GObject *object)
{
        CsmInhibitDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_INHIBIT_DIALOG (object));

        dialog = CSM_INHIBIT_DIALOG (object);

        g_return_if_fail (dialog != NULL);

        g_debug ("CsmInhibitDialog: finalizing");

        G_OBJECT_CLASS (csm_inhibit_dialog_parent_class)->finalize (object);
}

CtkWidget *
csm_inhibit_dialog_new (CsmStore *inhibitors,
                        CsmStore *clients,
                        int       action)
{
        GObject *object;

        object = g_object_new (CSM_TYPE_INHIBIT_DIALOG,
                               "action", action,
                               "inhibitor-store", inhibitors,
                               "client-store", clients,
                               NULL);

        return CTK_WIDGET (object);
}
