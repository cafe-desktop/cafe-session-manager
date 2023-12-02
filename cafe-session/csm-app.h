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

#ifndef __CSM_APP_H__
#define __CSM_APP_H__

#include <glib-object.h>
#include <sys/types.h>

#include "eggdesktopfile.h"

#include "csm-manager.h"
#include "csm-client.h"

G_BEGIN_DECLS

#define CSM_TYPE_APP            (csm_app_get_type ())
G_DECLARE_DERIVABLE_TYPE (CsmApp, csm_app, CSM, APP, GObject)

struct _CsmAppClass
{
        GObjectClass parent_class;

        /* signals */
        void        (*exited)       (CsmApp *app);
        void        (*died)         (CsmApp *app);
        void        (*registered)   (CsmApp *app);

        /* virtual methods */
        gboolean    (*impl_start)                     (CsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_restart)                   (CsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_stop)                      (CsmApp     *app,
                                                       GError    **error);
        int         (*impl_peek_autostart_delay)      (CsmApp     *app);
        gboolean    (*impl_provides)                  (CsmApp     *app,
                                                       const char *service);
        gboolean    (*impl_has_autostart_condition)   (CsmApp     *app,
                                                       const char *service);
        gboolean    (*impl_is_running)                (CsmApp     *app);

        gboolean    (*impl_get_autorestart)           (CsmApp     *app);
        const char *(*impl_get_app_id)                (CsmApp     *app);
        gboolean    (*impl_is_disabled)               (CsmApp     *app);
        gboolean    (*impl_is_conditionally_disabled) (CsmApp     *app);
};

typedef enum
{
        CSM_APP_ERROR_GENERAL = 0,
        CSM_APP_ERROR_START,
        CSM_APP_ERROR_STOP,
        CSM_APP_NUM_ERRORS
} CsmAppError;

#define CSM_APP_ERROR csm_app_error_quark ()

GQuark           csm_app_error_quark                    (void);

gboolean         csm_app_peek_autorestart               (CsmApp     *app);

const char      *csm_app_peek_id                        (CsmApp     *app);
const char      *csm_app_peek_app_id                    (CsmApp     *app);
const char      *csm_app_peek_startup_id                (CsmApp     *app);
CsmManagerPhase  csm_app_peek_phase                     (CsmApp     *app);
gboolean         csm_app_peek_is_disabled               (CsmApp     *app);
gboolean         csm_app_peek_is_conditionally_disabled (CsmApp     *app);

gboolean         csm_app_start                          (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_restart                        (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_stop                           (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_is_running                     (CsmApp     *app);

void             csm_app_exited                         (CsmApp     *app);
void             csm_app_died                           (CsmApp     *app);

gboolean         csm_app_provides                       (CsmApp     *app,
                                                         const char *service);
gboolean         csm_app_has_autostart_condition        (CsmApp     *app,
                                                         const char *condition);
void             csm_app_registered                     (CsmApp     *app);
int              csm_app_peek_autostart_delay           (CsmApp     *app);

/* exported to bus */
gboolean         csm_app_get_app_id                     (CsmApp     *app,
                                                         char      **id,
                                                         GError    **error);
gboolean         csm_app_get_startup_id                 (CsmApp     *app,
                                                         char      **id,
                                                         GError    **error);
gboolean         csm_app_get_phase                      (CsmApp     *app,
                                                         guint      *phase,
                                                         GError    **error);

G_END_DECLS

#endif /* __CSM_APP_H__ */
