/* cdm.h
 * Copyright (C) 2005 Raffaele Sandrini
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2002, 2003 George Lebl
 * Copyright (C) 2001 Queen of England,
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
 *      Raffaele Sandrini <rasa@gmx.ch>
 *      George Lebl <jirka@5z.com>
 *      Mark McLoughlin <mark@skynet.ie>
 */

#ifndef __CDM_LOGOUT_ACTION_H__
#define __CDM_LOGOUT_ACTION_H__

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CDM_LOGOUT_ACTION_NONE = 0,
	CDM_LOGOUT_ACTION_SHUTDOWN = 1 << 0,
	CDM_LOGOUT_ACTION_REBOOT = 1 << 1,
	CDM_LOGOUT_ACTION_SUSPEND = 1 << 2
} CdmLogoutAction;

gboolean cdm_is_available(void);

void cdm_new_login(void);

void cdm_set_logout_action(CdmLogoutAction action);

CdmLogoutAction cdm_get_logout_action(void);

gboolean cdm_supports_logout_action(CdmLogoutAction action);

#ifdef __cplusplus
}
#endif

#endif /* __CDM_LOGOUT_ACTION_H__ */
