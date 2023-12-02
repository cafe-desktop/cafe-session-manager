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

#ifndef __CSM_CLIENT_H__
#define __CSM_CLIENT_H__

#include <glib.h>
#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define CSM_TYPE_CLIENT            (csm_client_get_type ())
G_DECLARE_DERIVABLE_TYPE (GsmClient, csm_client, GSM, CLIENT, GObject)

typedef enum {
        CSM_CLIENT_UNREGISTERED = 0,
        CSM_CLIENT_REGISTERED,
        CSM_CLIENT_FINISHED,
        CSM_CLIENT_FAILED
} GsmClientStatus;

typedef enum {
        CSM_CLIENT_RESTART_NEVER = 0,
        CSM_CLIENT_RESTART_IF_RUNNING,
        CSM_CLIENT_RESTART_ANYWAY,
        CSM_CLIENT_RESTART_IMMEDIATELY
} GsmClientRestartStyle;

typedef enum {
        CSM_CLIENT_END_SESSION_FLAG_FORCEFUL = 1 << 0,
        CSM_CLIENT_END_SESSION_FLAG_SAVE     = 1 << 1,
        CSM_CLIENT_END_SESSION_FLAG_LAST     = 1 << 2
} GsmClientEndSessionFlag;

struct _GsmClientClass
{
        GObjectClass parent_class;

        /* signals */
        void         (*disconnected)               (GsmClient  *client);
        void         (*end_session_response)       (GsmClient  *client,
                                                    gboolean    ok,
                                                    gboolean    do_last,
                                                    gboolean    cancel,
                                                    const char *reason);

        /* virtual methods */
        char *                (*impl_get_app_name)           (GsmClient *client);
        GsmClientRestartStyle (*impl_get_restart_style_hint) (GsmClient *client);
        guint                 (*impl_get_unix_process_id)    (GsmClient *client);
        gboolean              (*impl_query_end_session)      (GsmClient *client,
                                                              guint      flags,
                                                              GError   **error);
        gboolean              (*impl_end_session)            (GsmClient *client,
                                                              guint      flags,
                                                              GError   **error);
        gboolean              (*impl_cancel_end_session)     (GsmClient *client,
                                                              GError   **error);
        gboolean              (*impl_stop)                   (GsmClient *client,
                                                              GError   **error);
        GKeyFile *            (*impl_save)                   (GsmClient *client,
                                                              GError   **error);
};

typedef enum
{
        CSM_CLIENT_ERROR_GENERAL = 0,
        CSM_CLIENT_ERROR_NOT_REGISTERED,
        CSM_CLIENT_NUM_ERRORS
} GsmClientError;

#define CSM_CLIENT_ERROR csm_client_error_quark ()
#define CSM_CLIENT_TYPE_ERROR (csm_client_error_get_type ())

GType                 csm_client_error_get_type             (void);
GQuark                csm_client_error_quark                (void);

const char           *csm_client_peek_id                    (GsmClient  *client);


const char *          csm_client_peek_startup_id            (GsmClient  *client);
const char *          csm_client_peek_app_id                (GsmClient  *client);
guint                 csm_client_peek_restart_style_hint    (GsmClient  *client);
guint                 csm_client_peek_status                (GsmClient  *client);


char                 *csm_client_get_app_name               (GsmClient  *client);
void                  csm_client_set_app_id                 (GsmClient  *client,
                                                             const char *app_id);
void                  csm_client_set_status                 (GsmClient  *client,
                                                             guint       status);

gboolean              csm_client_end_session                (GsmClient  *client,
                                                             guint       flags,
                                                             GError    **error);
gboolean              csm_client_query_end_session          (GsmClient  *client,
                                                             guint       flags,
                                                             GError    **error);
gboolean              csm_client_cancel_end_session         (GsmClient  *client,
                                                             GError    **error);

void                  csm_client_disconnected               (GsmClient  *client);

GKeyFile             *csm_client_save                       (GsmClient  *client,
                                                             GError    **error);
/* exported to bus */
gboolean              csm_client_stop                       (GsmClient  *client,
                                                             GError    **error);
gboolean              csm_client_get_startup_id             (GsmClient  *client,
                                                             char      **startup_id,
                                                             GError    **error);
gboolean              csm_client_get_app_id                 (GsmClient  *client,
                                                             char      **app_id,
                                                             GError    **error);
gboolean              csm_client_get_restart_style_hint     (GsmClient  *client,
                                                             guint      *hint,
                                                             GError    **error);
gboolean              csm_client_get_status                 (GsmClient  *client,
                                                             guint      *status,
                                                             GError    **error);
gboolean              csm_client_get_unix_process_id        (GsmClient  *client,
                                                             guint      *pid,
                                                             GError    **error);

/* private */

void                  csm_client_end_session_response       (GsmClient  *client,
                                                             gboolean    is_ok,
                                                             gboolean    do_last,
                                                             gboolean    cancel,
                                                             const char *reason);

G_END_DECLS

#endif /* __CSM_CLIENT_H__ */
