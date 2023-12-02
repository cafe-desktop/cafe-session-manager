/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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


#ifndef __CSM_MANAGER_H
#define __CSM_MANAGER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_MANAGER         (csm_manager_get_type ())
G_DECLARE_DERIVABLE_TYPE (CsmManager, csm_manager, CSM, MANAGER, GObject)

struct _CsmManagerClass
{
        GObjectClass   parent_class;

        void          (* session_running)     (CsmManager      *manager);
        void          (* session_over)        (CsmManager      *manager);
        void          (* session_over_notice) (CsmManager      *manager);

        void          (* phase_changed)       (CsmManager      *manager,
                                               const char      *phase);

        void          (* client_added)        (CsmManager      *manager,
                                               const char      *id);
        void          (* client_removed)      (CsmManager      *manager,
                                               const char      *id);
        void          (* inhibitor_added)     (CsmManager      *manager,
                                               const char      *id);
        void          (* inhibitor_removed)   (CsmManager      *manager,
                                               const char      *id);
}; //CsmManagerClass;

typedef enum {
        /* gsm's own startup/initialization phase */
        CSM_MANAGER_PHASE_STARTUP = 0,
        /* xrandr setup, cafe-settings-daemon, etc */
        CSM_MANAGER_PHASE_INITIALIZATION,
        /* window/compositing managers */
        CSM_MANAGER_PHASE_WINDOW_MANAGER,
        /* apps that will create _NET_WM_WINDOW_TYPE_PANEL windows */
        CSM_MANAGER_PHASE_PANEL,
        /* apps that will create _NET_WM_WINDOW_TYPE_DESKTOP windows */
        CSM_MANAGER_PHASE_DESKTOP,
        /* everything else */
        CSM_MANAGER_PHASE_APPLICATION,
        /* done launching */
        CSM_MANAGER_PHASE_RUNNING,
        /* shutting down */
        CSM_MANAGER_PHASE_QUERY_END_SESSION,
        CSM_MANAGER_PHASE_END_SESSION,
        CSM_MANAGER_PHASE_EXIT
} CsmManagerPhase;

typedef enum
{
        CSM_MANAGER_ERROR_GENERAL = 0,
        CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
        CSM_MANAGER_ERROR_NOT_IN_RUNNING,
        CSM_MANAGER_ERROR_ALREADY_REGISTERED,
        CSM_MANAGER_ERROR_NOT_REGISTERED,
        CSM_MANAGER_ERROR_INVALID_OPTION,
        CSM_MANAGER_ERROR_LOCKED_DOWN,
        CSM_MANAGER_NUM_ERRORS
} CsmManagerError;

#define CSM_MANAGER_ERROR csm_manager_error_quark ()

typedef enum {
        CSM_MANAGER_LOGOUT_MODE_NORMAL = 0,
        CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION,
        CSM_MANAGER_LOGOUT_MODE_FORCE
} CsmManagerLogoutMode;

GType               csm_manager_error_get_type                 (void);
#define CSM_MANAGER_TYPE_ERROR (csm_manager_error_get_type ())

GQuark              csm_manager_error_quark                    (void);

CsmManager *        csm_manager_new                            (CsmStore       *client_store,
                                                                gboolean        failsafe);

gboolean            csm_manager_add_autostart_app              (CsmManager     *manager,
                                                                const char     *path,
                                                                const char     *provides);
gboolean            csm_manager_add_autostart_apps_from_dir    (CsmManager     *manager,
                                                                const char     *path);
gboolean            csm_manager_add_legacy_session_apps        (CsmManager     *manager,
                                                                const char     *path);

void                csm_manager_start                          (CsmManager     *manager);


/* exported methods */

gboolean            csm_manager_register_client                (CsmManager            *manager,
                                                                const char            *app_id,
                                                                const char            *client_startup_id,
                                                                DBusGMethodInvocation *context);
gboolean            csm_manager_unregister_client              (CsmManager            *manager,
                                                                const char            *session_client_id,
                                                                DBusGMethodInvocation *context);

gboolean            csm_manager_inhibit                        (CsmManager            *manager,
                                                                const char            *app_id,
                                                                guint                  toplevel_xid,
                                                                const char            *reason,
                                                                guint                  flags,
                                                                DBusGMethodInvocation *context);
gboolean            csm_manager_uninhibit                      (CsmManager            *manager,
                                                                guint                  inhibit_cookie,
                                                                DBusGMethodInvocation *context);
gboolean            csm_manager_is_inhibited                   (CsmManager            *manager,
                                                                guint                  flags,
                                                                gboolean              *is_inhibited,
                                                                GError                *error);

gboolean            csm_manager_request_shutdown               (CsmManager     *manager,
                                                                GError        **error);

gboolean            csm_manager_request_reboot                 (CsmManager     *manager,
                                                                GError        **error);

gboolean            csm_manager_shutdown                       (CsmManager     *manager,
                                                                GError        **error);

gboolean            csm_manager_can_shutdown                   (CsmManager     *manager,
                                                                gboolean       *shutdown_available,
                                                                GError        **error);
gboolean            csm_manager_logout                         (CsmManager     *manager,
                                                                guint           logout_mode,
                                                                GError        **error);

gboolean            csm_manager_setenv                         (CsmManager     *manager,
                                                                const char     *variable,
                                                                const char     *value,
                                                                GError        **error);
gboolean            csm_manager_initialization_error           (CsmManager     *manager,
                                                                const char     *message,
                                                                gboolean        fatal,
                                                                GError        **error);

gboolean            csm_manager_get_clients                    (CsmManager     *manager,
                                                                GPtrArray     **clients,
                                                                GError        **error);
gboolean            csm_manager_get_inhibitors                 (CsmManager     *manager,
                                                                GPtrArray     **inhibitors,
                                                                GError        **error);
gboolean            csm_manager_is_autostart_condition_handled (CsmManager     *manager,
                                                                const char     *condition,
                                                                gboolean       *handled,
                                                                GError        **error);
gboolean            csm_manager_set_phase                      (CsmManager     *manager,
                                                                CsmManagerPhase phase);

gboolean            csm_manager_is_session_running             (CsmManager *manager,
                                                                gboolean *running,
                                                                GError **error);

void                _csm_manager_set_renderer                  (CsmManager     *manager,
                                                                const char     *renderer);

G_END_DECLS

#endif /* __CSM_MANAGER_H */
