/* csm-session-save.h
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

#ifndef __CSM_SESSION_SAVE_H__
#define __CSM_SESSION_SAVE_H__

#include <glib.h>

#include "csm-store.h"

#ifdef __cplusplus
extern "C" {
#endif

void      csm_session_save                 (CsmStore  *client_store,
                                            GError   **error);

#ifdef __cplusplus
}
#endif

#endif /* __CSM_SESSION_SAVE_H__ */
