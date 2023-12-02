/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

#ifndef __CDM_SIGNAL_HANDLER_H
#define __CDM_SIGNAL_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CDM_TYPE_SIGNAL_HANDLER (cdm_signal_handler_get_type())
G_DECLARE_FINAL_TYPE (MdmSignalHandler, cdm_signal_handler, CDM, SIGNAL_HANDLER, GObject)

typedef gboolean (*MdmSignalHandlerFunc)(int signal, gpointer data);

typedef void (*MdmShutdownHandlerFunc)(gpointer data);

typedef struct MdmSignalHandlerPrivate MdmSignalHandlerPrivate;

MdmSignalHandler* cdm_signal_handler_new(void);
void cdm_signal_handler_set_fatal_func(MdmSignalHandler* handler, MdmShutdownHandlerFunc func, gpointer user_data);

void cdm_signal_handler_add_fatal(MdmSignalHandler* handler);
guint cdm_signal_handler_add(MdmSignalHandler* handler, int signal_number, MdmSignalHandlerFunc callback, gpointer data);
void cdm_signal_handler_remove(MdmSignalHandler* handler, guint id);
void cdm_signal_handler_remove_func(MdmSignalHandler* handler, guint signal_number, MdmSignalHandlerFunc callback, gpointer data);

G_END_DECLS

#endif /* __CDM_SIGNAL_HANDLER_H */
