/* csm-util.h
 * Copyright (C) 2008 Lucas Rocha.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __CSM_UTIL_H__
#define __CSM_UTIL_H__

#include <glib.h>
#include <ctk/ctk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

gchar**     csm_get_screen_locker_command           (void);

char *      csm_util_find_desktop_file_for_app_name (const char  *app_name,
                                                     char       **dirs);

gchar      *csm_util_get_empty_tmp_session_dir      (void);

const char *csm_util_get_saved_session_dir          (void);

gchar**     csm_util_get_app_dirs                   (void);

gchar**     csm_util_get_autostart_dirs             (void);

gchar **    csm_util_get_desktop_dirs               (void);

gboolean    csm_util_text_is_blank                  (const char *str);

void        csm_util_init_error                     (gboolean    fatal,
                                                     const char *format, ...);

char *      csm_util_generate_startup_id            (void);

gboolean    csm_util_export_activation_environment  (GError     **error);

#ifdef HAVE_SYSTEMD
gboolean    csm_util_export_user_environment        (GError     **error);
#endif

void        csm_util_setenv                         (const char *variable,
                                                     const char *value);

CtkWidget*  csm_util_dialog_add_button              (CtkDialog   *dialog,
                                                     const gchar *button_text,
                                                     const gchar *icon_name,
                                                     gint         response_id);

#ifdef __cplusplus
}
#endif

#endif /* __CSM_UTIL_H__ */
