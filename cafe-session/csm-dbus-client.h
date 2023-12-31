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

#ifndef __CSM_DBUS_CLIENT_H__
#define __CSM_DBUS_CLIENT_H__

#include "csm-client.h"

G_BEGIN_DECLS

#define CSM_TYPE_DBUS_CLIENT            (csm_dbus_client_get_type ())
G_DECLARE_FINAL_TYPE (CsmDBusClient, csm_dbus_client, CSM, DBUS_CLIENT, CsmClient)

typedef enum
{
        CSM_DBUS_CLIENT_ERROR_GENERAL = 0,
        CSM_DBUS_CLIENT_ERROR_NOT_CLIENT,
        CSM_DBUS_CLIENT_NUM_ERRORS
} CsmDBusClientError;

#define CSM_DBUS_CLIENT_ERROR csm_dbus_client_error_quark ()

GType          csm_dbus_client_error_get_type     (void);
#define CSM_DBUS_CLIENT_TYPE_ERROR (csm_dbus_client_error_get_type ())

GQuark         csm_dbus_client_error_quark        (void);

CsmClient *    csm_dbus_client_new                (const char     *startup_id,
                                                   const char     *bus_name);
const char *   csm_dbus_client_get_bus_name       (CsmDBusClient  *client);

G_END_DECLS

#endif /* __CSM_DBUS_CLIENT_H__ */
