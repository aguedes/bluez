/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2011  Nokia Corporation
 *  Copyright (C) 2011  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <time.h>
#include <errno.h>
#include <bluetooth/uuid.h>
#include <bluetooth/sdp.h>
#include <dbus/dbus.h>
#include <gdbus.h>

#include "log.h"
#include "server.h"
#include "glib-helper.h"

#define TIMED_BUS_NAME		"com.nokia.time"
#define TIMED_PATH		"/com/nokia/time"
#define TIMED_CLOCK_IF		"com.nokia.time"
#define TIMED_CLOCK_INFO	"wall_clock_info"

static guint watch = 0;
static DBusConnection *connection = NULL;
static gboolean source_online = FALSE;
static uint8_t update_state = UPDATE_STATE_IDLE;
static uint8_t update_result = UPDATE_RESULT_NOT_ATTEMPTED;

uint8_t time_provider_control(int op)
{
	switch (op) {
	case GET_REFERENCE_UPDATE:
		if (update_state == UPDATE_STATE_PENDING)
			break;

		if (!source_online) {
			update_state = UPDATE_STATE_IDLE;
			update_result = UPDATE_RESULT_NO_CONN;
			break;
		}

		update_state = UPDATE_STATE_IDLE;
		update_result = UPDATE_RESULT_SUCCESSFUL;
		break;
	case CANCEL_REFERENCE_UPDATE:
		update_state = UPDATE_STATE_IDLE;
		update_result = UPDATE_RESULT_CANCELED;
		break;
	default:
		DBG("Invalid control point value: 0x%02x", op);
	}

	return 0;
}

void time_provider_status(uint8_t *state, uint8_t *result)
{
	*state = update_state;
	*result = update_result;
}

static gboolean settings_changed(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	DBusMessageIter iter, res;

	update_state = UPDATE_STATE_IDLE;

	dbus_message_iter_init(msg, &iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRUCT) {
		error("Unexpected signature");
		update_result = UPDATE_RESULT_ERROR;
		return TRUE;
	}

	dbus_message_iter_recurse(&iter, &res);
	if (dbus_message_iter_get_arg_type(&res) != DBUS_TYPE_BOOLEAN) {
		error("Unexpected signature");
		update_result = UPDATE_RESULT_ERROR;
		return TRUE;
	}

	dbus_message_iter_get_basic(&res, &source_online);
	if (!source_online) {
		update_result = UPDATE_RESULT_NO_CONN;
		DBG("going offline");
		return TRUE;
	}

	update_result = UPDATE_RESULT_SUCCESSFUL;
	DBG("time updated");

	return TRUE;
}

int time_provider_init(void)
{
	connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

	watch = g_dbus_add_signal_watch(connection, TIMED_BUS_NAME, NULL,
						TIMED_CLOCK_IF,
						"settings_changed",
						settings_changed,
						NULL, NULL);

	if (watch == 0) {
		dbus_connection_unref(connection);
		return -ENOMEM;
	}

	return 0;
}

void time_provider_exit(void)
{
	g_dbus_remove_watch(connection, watch);
	watch = 0;

	dbus_connection_unref(connection);
	connection = NULL;
}
