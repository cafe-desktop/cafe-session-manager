/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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

#ifndef __CSM_INHIBITOR_H__
#define __CSM_INHIBITOR_H__

#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define CSM_TYPE_INHIBITOR            (csm_inhibitor_get_type ())
#define CSM_INHIBITOR_ERROR           (csm_inhibitor_error_quark ())
#define CSM_INHIBITOR_TYPE_ERROR      (csm_inhibitor_error_get_type ())
G_DECLARE_FINAL_TYPE (CsmInhibitor, csm_inhibitor, CSM, INHIBITOR, GObject)

typedef enum {
        CSM_INHIBITOR_FLAG_LOGOUT      = 1 << 0,
        CSM_INHIBITOR_FLAG_SWITCH_USER = 1 << 1,
        CSM_INHIBITOR_FLAG_SUSPEND     = 1 << 2,
        CSM_INHIBITOR_FLAG_IDLE        = 1 << 3
} CsmInhibitorFlag;

typedef enum
{
        CSM_INHIBITOR_ERROR_GENERAL = 0,
        CSM_INHIBITOR_ERROR_NOT_SET,
        CSM_INHIBITOR_NUM_ERRORS
} CsmInhibitorError;

GType          csm_inhibitor_error_get_type       (void);

GQuark         csm_inhibitor_error_quark          (void);

CsmInhibitor * csm_inhibitor_new                  (const char    *app_id,
                                                   guint          toplevel_xid,
                                                   guint          flags,
                                                   const char    *reason,
                                                   const char    *bus_name,
                                                   guint          cookie);
CsmInhibitor * csm_inhibitor_new_for_client       (const char    *client_id,
                                                   const char    *app_id,
                                                   guint          flags,
                                                   const char    *reason,
                                                   const char    *bus_name,
                                                   guint          cookie);

const char *   csm_inhibitor_peek_id              (CsmInhibitor  *inhibitor);
const char *   csm_inhibitor_peek_app_id          (CsmInhibitor  *inhibitor);
const char *   csm_inhibitor_peek_client_id       (CsmInhibitor  *inhibitor);
const char *   csm_inhibitor_peek_reason          (CsmInhibitor  *inhibitor);
const char *   csm_inhibitor_peek_bus_name        (CsmInhibitor  *inhibitor);
guint          csm_inhibitor_peek_cookie          (CsmInhibitor  *inhibitor);
guint          csm_inhibitor_peek_flags           (CsmInhibitor  *inhibitor);
guint          csm_inhibitor_peek_toplevel_xid    (CsmInhibitor  *inhibitor);

/* exported to bus */
gboolean       csm_inhibitor_get_app_id           (CsmInhibitor  *inhibitor,
                                                   char         **id,
                                                   GError       **error);
gboolean       csm_inhibitor_get_client_id        (CsmInhibitor  *inhibitor,
                                                   char         **id,
                                                   GError       **error);
gboolean       csm_inhibitor_get_reason           (CsmInhibitor  *inhibitor,
                                                   char         **reason,
                                                   GError       **error);
gboolean       csm_inhibitor_get_flags            (CsmInhibitor  *inhibitor,
                                                   guint         *flags,
                                                   GError       **error);
gboolean       csm_inhibitor_get_toplevel_xid     (CsmInhibitor  *inhibitor,
                                                   guint         *xid,
                                                   GError       **error);

G_END_DECLS

#endif /* __CSM_INHIBITOR_H__ */
