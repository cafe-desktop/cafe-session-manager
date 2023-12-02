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

#ifndef __CSM_INHIBIT_DIALOG_H
#define __CSM_INHIBIT_DIALOG_H

#include <glib-object.h>
#include <ctk/ctk.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_INHIBIT_DIALOG         (gsm_inhibit_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GsmInhibitDialog, gsm_inhibit_dialog, GSM, INHIBIT_DIALOG, CtkDialog)

typedef enum
{
        CSM_LOGOUT_ACTION_LOGOUT,
        CSM_LOGOUT_ACTION_SWITCH_USER,
        CSM_LOGOUT_ACTION_SHUTDOWN,
        CSM_LOGOUT_ACTION_REBOOT,
        CSM_LOGOUT_ACTION_HIBERNATE,
        CSM_LOGOUT_ACTION_SLEEP
} GsmLogoutAction;

CtkWidget            * gsm_inhibit_dialog_new                (GsmStore         *inhibitors,
                                                              GsmStore         *clients,
                                                              int               action);
CtkTreeModel         * gsm_inhibit_dialog_get_model          (GsmInhibitDialog *dialog);

G_END_DECLS

#endif /* __CSM_INHIBIT_DIALOG_H */
