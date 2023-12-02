/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 Raffaele Sandrini
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2002, 2003 George Lebl
 * Copyright (C) 2001 Queen of England,
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
 *      Raffaele Sandrini <rasa@gmx.ch>
 *      George Lebl <jirka@5z.com>
 *      Mark McLoughlin <mark@skynet.ie>
 */

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <X11/Xauth.h>
#include <cdk/cdk.h>

#include "cdm.h"

#define CDM_PROTOCOL_UPDATE_INTERVAL 1 /* seconds */

#define CDM_PROTOCOL_SOCKET_PATH "/var/run/cdm_socket"

#define CDM_PROTOCOL_MSG_CLOSE "CLOSE"
#define CDM_PROTOCOL_MSG_VERSION "VERSION"
#define CDM_PROTOCOL_MSG_AUTHENTICATE "AUTH_LOCAL"
#define CDM_PROTOCOL_MSG_QUERY_ACTION "QUERY_LOGOUT_ACTION"
#define CDM_PROTOCOL_MSG_SET_ACTION "SET_SAFE_LOGOUT_ACTION"
#define CDM_PROTOCOL_MSG_FLEXI_XSERVER "FLEXI_XSERVER"

#define CDM_ACTION_STR_NONE "NONE"
#define CDM_ACTION_STR_SHUTDOWN "HALT"
#define CDM_ACTION_STR_REBOOT "REBOOT"
#define CDM_ACTION_STR_SUSPEND "SUSPEND"

typedef struct {
	int fd;
	char* auth_cookie;

	MdmLogoutAction available_actions;
	MdmLogoutAction current_actions;

	time_t last_update;
} MdmProtocolData;

static MdmProtocolData cdm_protocol_data = {
	0,
	NULL,
	CDM_LOGOUT_ACTION_NONE,
	CDM_LOGOUT_ACTION_NONE,
	0
};

static char* cdm_send_protocol_msg(MdmProtocolData* data, const char* msg)
{
	GString* retval;
	char buf[256];
	char* p;
	int len;

	p = g_strconcat(msg, "\n", NULL);

	if (write(data->fd, p, strlen(p)) < 0)
	{
		g_free(p);

		g_warning("Failed to send message to CDM: %s", g_strerror(errno));

		return NULL;
	}

	g_free(p);

	p = NULL;
	retval = NULL;

	while ((len = read(data->fd, buf, sizeof(buf) - 1)) > 0)
	{
		buf[len] = '\0';

		if (!retval)
		{
			retval = g_string_new(buf);
		}
		else
		{
			retval = g_string_append(retval, buf);
		}

		if ((p = strchr(retval->str, '\n')))
		{
			break;
		}
	}

	if (p)
	{
		*p = '\0';
	}

	return retval ? g_string_free(retval, FALSE) : NULL;
}

static char* get_display_number(void)
{
	const char* display_name;
	char* retval;
	char* p;

	display_name = cdk_display_get_name(cdk_display_get_default());

	p = strchr(display_name, ':');

	if (!p)
	{
		return g_strdup("0");
	}

	while (*p == ':')
	{
		p++;
	}

	retval = g_strdup(p);

	p = strchr(retval, '.');

	if (p != NULL)
	{
		*p = '\0';
	}

	return retval;
}

static gboolean cdm_authenticate_connection(MdmProtocolData* data)
{
	#define CDM_MIT_MAGIC_COOKIE_LEN 16

	const char* xau_path;
	FILE* f;
	Xauth* xau;
	char* display_number;
	gboolean retval;

	if (data->auth_cookie)
	{
		char* msg;
		char* response;

		msg = g_strdup_printf(CDM_PROTOCOL_MSG_AUTHENTICATE " %s", data->auth_cookie);
		response = cdm_send_protocol_msg(data, msg);
		g_free(msg);

		if (response && !strcmp(response, "OK"))
		{
			g_free(response);
			return TRUE;
		}
		else
		{
			g_free(response);
			g_free(data->auth_cookie);
			data->auth_cookie = NULL;
		}
	}

	if (!(xau_path = XauFileName()))
	{
		return FALSE;
	}

	if (!(f = fopen(xau_path, "r")))
	{
		return FALSE;
	}

	retval = FALSE;
	display_number = get_display_number();

	while ((xau = XauReadAuth(f)))
	{
		char buffer[40]; /* 2*16 == 32, so 40 is enough */
		char* msg;
		char* response;
		int   i;

		if (xau->family != FamilyLocal || strncmp(xau->number, display_number, xau->number_length) || strncmp(xau->name, "MIT-MAGIC-COOKIE-1", xau->name_length) || xau->data_length != CDM_MIT_MAGIC_COOKIE_LEN)
		{
			XauDisposeAuth(xau);
			continue;
		}

		for (i = 0; i < CDM_MIT_MAGIC_COOKIE_LEN; i++)
		{
			g_snprintf(buffer + 2 * i, 3, "%02x", (guint)(guchar) xau->data[i]);
		}

		XauDisposeAuth(xau);

		msg = g_strdup_printf(CDM_PROTOCOL_MSG_AUTHENTICATE " %s", buffer);
		response = cdm_send_protocol_msg(data, msg);
		g_free(msg);

		if (response && !strcmp(response, "OK"))
		{
			data->auth_cookie = g_strdup(buffer);
			g_free(response);
			retval = TRUE;
			break;
		}

		g_free(response);
	}

	g_free(display_number);

	fclose(f);

	return retval;

	#undef CDM_MIT_MAGIC_COOKIE_LEN
}

static void cdm_shutdown_protocol_connection(MdmProtocolData *data)
{
	if (data->fd)
	{
		close(data->fd);
	}

	data->fd = 0;
}

static gboolean cdm_init_protocol_connection(MdmProtocolData* data)
{
	struct sockaddr_un addr;
	char* response;

	g_assert(data->fd <= 0);

	if (g_file_test(CDM_PROTOCOL_SOCKET_PATH, G_FILE_TEST_EXISTS))
	{
		g_strlcpy (addr.sun_path, CDM_PROTOCOL_SOCKET_PATH, sizeof (addr.sun_path));
	}
	else if (g_file_test("/tmp/.cdm_socket", G_FILE_TEST_EXISTS))
	{
		g_strlcpy (addr.sun_path, "/tmp/.cdm_socket", sizeof (addr.sun_path));
	}
	else
	{
		return FALSE;
	}

	data->fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (data->fd < 0)
	{
		g_warning("Failed to create CDM socket: %s", g_strerror(errno));

		cdm_shutdown_protocol_connection(data);

		return FALSE;
	}

	addr.sun_family = AF_UNIX;

	if (connect(data->fd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
	{
		g_warning("Failed to establish a connection with CDM: %s", g_strerror(errno));

		cdm_shutdown_protocol_connection(data);

		return FALSE;
	}

	response = cdm_send_protocol_msg(data, CDM_PROTOCOL_MSG_VERSION);

	if (!response || strncmp(response, "CDM ", strlen("CDM ")) != 0)
	{
		g_free(response);

		g_warning("Failed to get protocol version from CDM");
		cdm_shutdown_protocol_connection(data);

		return FALSE;
	}

	g_free(response);

	if (!cdm_authenticate_connection(data))
	{
		g_warning("Failed to authenticate with CDM");
		cdm_shutdown_protocol_connection(data);
		return FALSE;
	}

	return TRUE;
}

static void cdm_parse_query_response(MdmProtocolData* data, const char* response)
{
	char** actions;
	int i;

	data->available_actions = CDM_LOGOUT_ACTION_NONE;
	data->current_actions = CDM_LOGOUT_ACTION_NONE;

	if (strncmp(response, "OK ", 3) != 0)
	{
		return;
	}

	response += 3;

	actions = g_strsplit(response, ";", -1);

	for (i = 0; actions[i]; i++)
	{
		MdmLogoutAction action = CDM_LOGOUT_ACTION_NONE;
		gboolean selected = FALSE;
		char* str = actions [i];
		int len;

		len = strlen(str);

		if (!len)
		{
			continue;
		}

		if (str[len - 1] == '!')
		{
			selected = TRUE;
			str[len - 1] = '\0';
		}

		if (!strcmp(str, CDM_ACTION_STR_SHUTDOWN))
		{
				action = CDM_LOGOUT_ACTION_SHUTDOWN;
		}
		else if (!strcmp(str, CDM_ACTION_STR_REBOOT))
		{
				action = CDM_LOGOUT_ACTION_REBOOT;
		}
		else if (!strcmp(str, CDM_ACTION_STR_SUSPEND))
		{
				action = CDM_LOGOUT_ACTION_SUSPEND;
		}

		data->available_actions |= action;

		if (selected)
		{
			data->current_actions |= action;
		}
	}

	g_strfreev(actions);
}

static void cdm_update_logout_actions(MdmProtocolData* data)
{
	time_t current_time;
	char* response;

	current_time = time(NULL);

	if (current_time <= (data->last_update + CDM_PROTOCOL_UPDATE_INTERVAL))
	{
		return;
	}

	data->last_update = current_time;

	if (!cdm_init_protocol_connection(data))
	{
		return;
	}

	if ((response = cdm_send_protocol_msg(data, CDM_PROTOCOL_MSG_QUERY_ACTION)))
	{
		cdm_parse_query_response(data, response);
		g_free(response);
	}

	cdm_shutdown_protocol_connection(data);
}

gboolean cdm_is_available(void)
{
	if (!cdm_init_protocol_connection(&cdm_protocol_data))
	{
		return FALSE;
	}

	cdm_shutdown_protocol_connection(&cdm_protocol_data);

	return TRUE;
}

gboolean cdm_supports_logout_action(MdmLogoutAction action)
{
	cdm_update_logout_actions(&cdm_protocol_data);

	return (cdm_protocol_data.available_actions & action) != 0;
}

MdmLogoutAction cdm_get_logout_action(void)
{
	cdm_update_logout_actions(&cdm_protocol_data);

	return cdm_protocol_data.current_actions;
}

void cdm_set_logout_action(MdmLogoutAction action)
{
	char* action_str = NULL;
	char* msg;
	char* response;

	if (!cdm_init_protocol_connection(&cdm_protocol_data))
	{
		return;
	}

	switch (action)
	{
		case CDM_LOGOUT_ACTION_NONE:
			action_str = CDM_ACTION_STR_NONE;
			break;
		case CDM_LOGOUT_ACTION_SHUTDOWN:
			action_str = CDM_ACTION_STR_SHUTDOWN;
			break;
		case CDM_LOGOUT_ACTION_REBOOT:
			action_str = CDM_ACTION_STR_REBOOT;
			break;
		case CDM_LOGOUT_ACTION_SUSPEND:
			action_str = CDM_ACTION_STR_SUSPEND;
			break;
	}

	msg = g_strdup_printf(CDM_PROTOCOL_MSG_SET_ACTION " %s", action_str);

	response = cdm_send_protocol_msg(&cdm_protocol_data, msg);

	g_free(msg);
	g_free(response);

	cdm_protocol_data.last_update = 0;

	cdm_shutdown_protocol_connection(&cdm_protocol_data);
}

void cdm_new_login(void)
{
    char* response;

    if (!cdm_init_protocol_connection(&cdm_protocol_data))
    {
        return;
    }

    response = cdm_send_protocol_msg(&cdm_protocol_data, CDM_PROTOCOL_MSG_FLEXI_XSERVER);

    g_free(response);

    cdm_protocol_data.last_update = 0;

    cdm_shutdown_protocol_connection(&cdm_protocol_data);
}
