/*
 * Copyright (c) 2004-2005 Benedikt Meurer <benny@xfce.org>
 *               2013 Stefano Karapetsas <stefano@karapetsas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 * Most parts of this file where taken from xfce4-session and
 * gnome-session.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <ctk/ctk.h>
#include <cdk/cdkx.h>
#include <gio/gio.h>

#include "msm-gnome.h"

#define CSM_SCHEMA "org.cafe.session"
#define CSM_GNOME_COMPAT_STARTUP_KEY "gnome-compat-startup"

#define GNOME_KEYRING_DAEMON "gnome-keyring-daemon"


static gboolean gnome_compat_started = FALSE;
static Window gnome_smproxy_window = None;

static void
gnome_keyring_daemon_finished (GPid pid,
                               gint status,
                               gpointer user_data)
{
  if (WEXITSTATUS (status) != 0)
    {
      /* daemon failed for some reason */
      g_printerr ("gnome-keyring-daemon failed to start correctly, "
                  "exit code: %d\n", WEXITSTATUS (status));
    }
}

static void
gnome_keyring_daemon_startup (void)
{
  GError      *error = NULL;
  GPid         pid;
  gchar       *argv[3];

  error = NULL;
  argv[0] = GNOME_KEYRING_DAEMON;
  argv[1] = "--start";
  argv[2] = NULL;
  g_spawn_async (NULL, argv, NULL,
		 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
		 NULL, NULL, &pid,
		 &error);

  if (error != NULL)
    {
      g_printerr ("Failed to spawn gnome-keyring-daemon: %s\n",
                  error->message);
      g_error_free (error);
      return;
    }

  g_child_watch_add (pid, gnome_keyring_daemon_finished, NULL);
}



static void
msm_compat_gnome_smproxy_startup (void)
{
  Atom gnome_sm_proxy;
  Display *dpy;
  Window root;
  CdkDisplay *cdkdisplay;

  cdkdisplay = cdk_display_get_default ();
  cdk_x11_display_error_trap_push (cdkdisplay);

  /* Set GNOME_SM_PROXY property, since some apps (like OOo) seem to require
   * it to behave properly. Thanks to Jasper/Francois for reporting this.
   * This has another advantage, since it prevents people from running
   * gnome-smproxy in xfce4, which would cause trouble otherwise.
   */
  dpy = CDK_DISPLAY_XDISPLAY(cdkdisplay);
  root = RootWindow (dpy, 0);

  if (gnome_smproxy_window != None)
    XDestroyWindow (dpy, gnome_smproxy_window);

  gnome_sm_proxy = XInternAtom (dpy, "GNOME_SM_PROXY", False);
  gnome_smproxy_window = XCreateSimpleWindow (dpy, root, 1, 1, 1, 1, 0, 0, 0);

  XChangeProperty (dpy, gnome_smproxy_window, gnome_sm_proxy,
                   XA_CARDINAL, 32, PropModeReplace,
                   (unsigned char *) (void *) &gnome_smproxy_window, 1);
  XChangeProperty (dpy, root, gnome_sm_proxy,
                   XA_CARDINAL, 32, PropModeReplace,
                   (unsigned char *) (void *) &gnome_smproxy_window, 1);

  XSync (dpy, False);
  cdk_x11_display_error_trap_pop_ignored (cdkdisplay);
}


static void
msm_compat_gnome_smproxy_shutdown (void)
{
  CdkDisplay *cdkdisplay;

  cdkdisplay = cdk_display_get_default ();
  cdk_x11_display_error_trap_push (cdkdisplay);

  if (gnome_smproxy_window != None)
    {
      XDestroyWindow (CDK_DISPLAY_XDISPLAY(cdkdisplay), gnome_smproxy_window);
      XSync (CDK_DISPLAY_XDISPLAY(cdkdisplay), False);
      gnome_smproxy_window = None;
    }
  cdk_x11_display_error_trap_pop_ignored (cdkdisplay);
}


void
msm_gnome_start (void)
{
  GSettings* settings;
  gchar **array;

  if (gnome_compat_started == TRUE)
    return;

  settings = g_settings_new (CSM_SCHEMA);
  array = g_settings_get_strv (settings, CSM_GNOME_COMPAT_STARTUP_KEY);
  if (array) {
    guint i;

    for (i = 0; array[i]; i++) {
      if (strcmp (array[i], "smproxy") == 0) {
        g_debug ("MsmGnome: starting smproxy");
        msm_compat_gnome_smproxy_startup ();
        gnome_compat_started = TRUE;
      } else if (strcmp (array[i], "keyring") == 0) {
        g_debug ("MsmGnome: starting keyring");
        gnome_keyring_daemon_startup ();
        gnome_compat_started = TRUE;
      } else {
        g_debug ("MsmGnome: ignoring unknown component \"%s\"", array[i]);
      }
    }
    g_strfreev (array);
  } else {
    g_debug ("MsmGnome: No components found to start");
  }
  g_object_unref (settings);
}


void
msm_gnome_stop (void)
{
  if (gnome_compat_started == FALSE)
    return;

  g_debug ("MsmGnome: stopping");

  msm_compat_gnome_smproxy_shutdown ();

  gnome_compat_started = FALSE;
}

