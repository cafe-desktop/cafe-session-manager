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

#ifndef __CSM_PROPERTIES_DIALOG_H
#define __CSM_PROPERTIES_DIALOG_H

#include <glib-object.h>
#include <ctk/ctk.h>

G_BEGIN_DECLS

#define CSM_TYPE_PROPERTIES_DIALOG         (csm_properties_dialog_get_type ())
G_DECLARE_FINAL_TYPE (CsmPropertiesDialog, csm_properties_dialog, CSM, PROPERTIES_DIALOG, CtkDialog)

CtkWidget            * csm_properties_dialog_new                (void);

#define CSM_PROPERTIES_ICON_SIZE CTK_ICON_SIZE_LARGE_TOOLBAR

G_END_DECLS

#endif /* __CSM_PROPERTIES_DIALOG_H */
