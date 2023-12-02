/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
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
 *	Jon McCann <jmccann@redhat.com>
 */

#ifndef __CSM_CONSOLEKIT_H__
#define __CSM_CONSOLEKIT_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_CONSOLEKIT             (csm_consolekit_get_type ())
#define CSM_CONSOLEKIT_ERROR            (csm_consolekit_error_quark ())

G_DECLARE_DERIVABLE_TYPE (CsmConsolekit, csm_consolekit, GSM, CONSOLEKIT, GObject)

typedef enum   _CsmConsolekitError   CsmConsolekitError;

struct _CsmConsolekitClass
{
        GObjectClass parent_class;

        void (* request_completed) (CsmConsolekit *manager,
                                    GError        *error);

        void (* privileges_completed) (CsmConsolekit *manager,
                                       gboolean       success,
                                       gboolean       ask_later,
                                       GError        *error);
};

enum _CsmConsolekitError {
        CSM_CONSOLEKIT_ERROR_RESTARTING = 0,
        CSM_CONSOLEKIT_ERROR_STOPPING
};

#define CSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW "LoginWindow"

GQuark           csm_consolekit_error_quark     (void);

CsmConsolekit   *csm_consolekit_new             (void) G_GNUC_MALLOC;

gboolean         csm_consolekit_can_switch_user (CsmConsolekit *manager);

gboolean         csm_consolekit_get_restart_privileges (CsmConsolekit *manager);

gboolean         csm_consolekit_get_stop_privileges    (CsmConsolekit *manager);

gboolean         csm_consolekit_can_stop        (CsmConsolekit *manager);

gboolean         csm_consolekit_can_restart     (CsmConsolekit *manager);

gboolean         csm_consolekit_can_suspend     (CsmConsolekit *manager);

gboolean         csm_consolekit_can_hibernate   (CsmConsolekit *manager);

void             csm_consolekit_attempt_stop    (CsmConsolekit *manager);

void             csm_consolekit_attempt_restart (CsmConsolekit *manager);

void             csm_consolekit_attempt_suspend (CsmConsolekit *manager);

void             csm_consolekit_attempt_hibernate (CsmConsolekit *manager);

void             csm_consolekit_set_session_idle (CsmConsolekit *manager,
                                                  gboolean       is_idle);

gchar           *csm_consolekit_get_current_session_type (CsmConsolekit *manager);

CsmConsolekit   *csm_get_consolekit             (void);

G_END_DECLS

#endif /* __CSM_CONSOLEKIT_H__ */
