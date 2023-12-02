/*
 * Copyright (C) 2013 Stefano Karapetsas
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
 *  Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifndef __CSM_SYSTEMD_H__
#define __CSM_SYSTEMD_H__

#include <unistd.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_SYSTEMD             (csm_systemd_get_type ())
#define CSM_SYSTEMD_ERROR            (csm_systemd_error_quark ())
G_DECLARE_DERIVABLE_TYPE (CsmSystemd, csm_systemd, CSM, SYSTEMD, GObject)

#define LOGIND_RUNNING() (access("/run/systemd/seats/", F_OK) >= 0)
typedef enum   _CsmSystemdError   CsmSystemdError;

struct _CsmSystemdClass
{
        GObjectClass parent_class;

        void (* request_completed) (CsmSystemd *manager,
                                    GError        *error);

        void (* privileges_completed) (CsmSystemd *manager,
                                       gboolean       success,
                                       gboolean       ask_later,
                                       GError        *error);
};

enum _CsmSystemdError {
        CSM_SYSTEMD_ERROR_RESTARTING = 0,
        CSM_SYSTEMD_ERROR_STOPPING
};

#define CSM_SYSTEMD_SESSION_TYPE_LOGIN_WINDOW "greeter"

GQuark           csm_systemd_error_quark     (void);

CsmSystemd      *csm_systemd_new             (void) G_GNUC_MALLOC;

gboolean         csm_systemd_can_switch_user (CsmSystemd *manager);

gboolean         csm_systemd_get_restart_privileges (CsmSystemd *manager);

gboolean         csm_systemd_get_stop_privileges    (CsmSystemd *manager);

gboolean         csm_systemd_can_stop        (CsmSystemd *manager);

gboolean         csm_systemd_can_restart     (CsmSystemd *manager);

gboolean         csm_systemd_can_hibernate     (CsmSystemd *manager);

gboolean         csm_systemd_can_suspend     (CsmSystemd *manager);

gboolean         csm_systemd_is_last_session_for_user (CsmSystemd *manager);

void             csm_systemd_attempt_stop    (CsmSystemd *manager);

void             csm_systemd_attempt_restart (CsmSystemd *manager);

void             csm_systemd_attempt_hibernate (CsmSystemd *manager);

void             csm_systemd_attempt_suspend (CsmSystemd *manager);

void             csm_systemd_set_session_idle (CsmSystemd *manager,
                                                  gboolean       is_idle);

gchar           *csm_systemd_get_current_session_type (CsmSystemd *manager);

CsmSystemd      *csm_get_systemd             (void);

G_END_DECLS

#endif /* __CSM_SYSTEMD_H__ */
