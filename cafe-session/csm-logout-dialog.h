/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Vincent Untz
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

#ifndef __CSM_LOGOUT_DIALOG_H__
#define __CSM_LOGOUT_DIALOG_H__

#include <ctk/ctk.h>

G_BEGIN_DECLS

#define CSM_TYPE_LOGOUT_DIALOG         (csm_logout_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GsmLogoutDialog, csm_logout_dialog, GSM, LOGOUT_DIALOG, CtkMessageDialog)

enum
{
        CSM_LOGOUT_RESPONSE_LOGOUT,
        CSM_LOGOUT_RESPONSE_SWITCH_USER,
        CSM_LOGOUT_RESPONSE_SHUTDOWN,
        CSM_LOGOUT_RESPONSE_REBOOT,
        CSM_LOGOUT_RESPONSE_HIBERNATE,
        CSM_LOGOUT_RESPONSE_SLEEP
};

CtkWidget   *csm_get_logout_dialog        (CdkScreen           *screen,
                                           guint32              activate_time);
CtkWidget   *csm_get_shutdown_dialog      (CdkScreen           *screen,
                                           guint32              activate_time);

G_END_DECLS

#endif /* __CSM_LOGOUT_DIALOG_H__ */
