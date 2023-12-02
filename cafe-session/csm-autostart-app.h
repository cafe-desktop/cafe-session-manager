/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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

#ifndef __CSM_AUTOSTART_APP_H__
#define __CSM_AUTOSTART_APP_H__

#include "csm-app.h"

#include <X11/SM/SMlib.h>

G_BEGIN_DECLS

#define CSM_TYPE_AUTOSTART_APP            (csm_autostart_app_get_type ())
G_DECLARE_DERIVABLE_TYPE (CsmAutostartApp, csm_autostart_app, GSM, AUTOSTART_APP, CsmApp)

struct _CsmAutostartAppClass
{
        CsmAppClass parent_class;

        /* signals */
        void     (*condition_changed)  (CsmApp  *app,
                                        gboolean condition);
};

CsmApp *csm_autostart_app_new                (const char *desktop_file);

#define CSM_AUTOSTART_APP_ENABLED_KEY     "X-CAFE-Autostart-enabled"
#define CSM_AUTOSTART_APP_PHASE_KEY       "X-CAFE-Autostart-Phase"
#define CSM_AUTOSTART_APP_PROVIDES_KEY    "X-CAFE-Provides"
#define CSM_AUTOSTART_APP_STARTUP_ID_KEY  "X-CAFE-Autostart-startup-id"
#define CSM_AUTOSTART_APP_AUTORESTART_KEY "X-CAFE-AutoRestart"
#define CSM_AUTOSTART_APP_DBUS_NAME_KEY   "X-CAFE-DBus-Name"
#define CSM_AUTOSTART_APP_DBUS_PATH_KEY   "X-CAFE-DBus-Path"
#define CSM_AUTOSTART_APP_DBUS_ARGS_KEY   "X-CAFE-DBus-Start-Arguments"
#define CSM_AUTOSTART_APP_DISCARD_KEY     "X-CAFE-Autostart-discard-exec"
#define CSM_AUTOSTART_APP_DELAY_KEY       "X-CAFE-Autostart-Delay"

G_END_DECLS

#endif /* __CSM_AUTOSTART_APP_H__ */
