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
#include <gdbus.h>
#include <errno.h>
#include <bluetooth/uuid.h>

#include "att.h"
#include "error.h"
#include "gattrib.h"
#include "attrib-server.h"
#include "gatt-service.h"
#include "log.h"
#include "server.h"

#define PHONE_ALERT_STATUS_SVC_UUID		0x180E

#define ALERT_STATUS_CHR_UUID		0x2A3F
#define RINGER_CP_CHR_UUID		0x2A40
#define RINGER_SETTING_CHR_UUID		0x2A41

#define AGENT_INTERFACE "org.bluez.PhoneAgent"
#define ALERT_INTERFACE "org.bluez.PhoneAlert"
#define ALERT_PATH "/test/phonealert"

/* OHM plugin D-Bus definitions */
#define OHM_BUS_NAME		"com.nokia.NonGraphicFeedback1"
#define OHM_INTERFACE		"com.nokia.NonGraphicFeedback1"
#define OHM_PATH		"/com/nokia/NonGraphicFeedback1"

enum {
	ALERT_RINGER_STATE = 1 << 0,
	ALERT_VIBRATOR_STATE = 1 << 1,
	ALERT_DISPLAY_STATE = 1 << 2,
};

enum {
	SET_SILENT_MODE = 1,
	MUTE_ONCE,
	CANCEL_SILENT_MODE,
};

enum {
	RINGER_SILENT = 0,
	RINGER_NORMAL = 1,
};

struct agent {
	char *name;
	char *path;
	guint listener_id;
};

static DBusConnection *connection = NULL;
static uint8_t ringer_setting = 0xff;
static uint8_t alert_status = 0xff;
static uint16_t handle_ringer_setting = 0x0000;
static uint16_t handle_alert_status = 0x0000;
static struct agent agent;

static void agent_operation(const char *operation)
{
	DBusMessage *message;

	if (!agent.name) {
		error("Agent not registered");
		return;
	}

	DBG("%s: agent %s, %s", operation, agent.name, agent.path);

	message = dbus_message_new_method_call(agent.name, agent.path,
						AGENT_INTERFACE, operation);

	if (message == NULL) {
		error("Couldn't allocate D-Bus message");
		return;
	}

	if (!g_dbus_send_message(connection, message))
		error("D-Bus error: agent_operation %s", operation);
}

static void stop_ringtone(void)
{
	DBusMessage *message;

	message = dbus_message_new_method_call(OHM_BUS_NAME, OHM_PATH,
					OHM_INTERFACE, "StopRingtone");
	if (message == NULL) {
		error("Couldn't allocate D-Bus message");
		return;
	}

	if (!g_dbus_send_message(connection, message))
		error("Failed to send D-Bus message");
}

static uint8_t control_point_write(struct attribute *a, gpointer user_data)
{
	DBG("a = %p", a);

	switch (a->data[0]) {
	case SET_SILENT_MODE:
		agent_operation("SetSilentMode");
		break;
	case MUTE_ONCE:
		stop_ringtone();
		break;
	case CANCEL_SILENT_MODE:
		agent_operation("CancelSilentMode");
		break;
	default:
		DBG("Unknown mode");
	}

	return 0;
}

static void get_alert_reply(DBusPendingCall *call, void *user_data)
{
	DBusError derr;
	DBusMessage *reply;
	DBusMessageIter iter;
	uint8_t state;

	reply = dbus_pending_call_steal_reply(call);

	dbus_error_init(&derr);
	if (dbus_set_error_from_message(&derr, reply)) {
		error("D-Bus replied with error: %s, %s", derr.name,
								derr.message);
		dbus_error_free(&derr);
		goto done;
	}

	dbus_message_iter_init(reply, &iter);
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BYTE) {
		error("Unexpected signature for reply");
		goto done;
	}

	dbus_message_iter_get_basic(&iter, &state);

	DBG("Ringer State: %s",
			state & ALERT_RINGER_STATE ? "Active": "Not Active");

	alert_status = state;

done:
	dbus_message_unref(reply);
	dbus_pending_call_unref(call);
}

static void get_alert_status(DBusConnection *conn, void *user_data)
{
	DBusMessage *msg;
	DBusPendingCall *call;

	if (!agent.name) {
		error("Agent not registered");
		return;
	}

	DBG("Get Alert Status: agent %s, %s", agent.name, agent.path);

	msg = dbus_message_new_method_call(agent.name, agent.path,
					AGENT_INTERFACE, "GetAlertStatus");
	if (msg == NULL) {
		error("Unable to allocate new D-Bus message");
		return;
	}

	if (!dbus_connection_send_with_reply(connection, msg, &call, -1)) {
		error("Failed to send D-Bus message");
		return;
	}

	dbus_pending_call_set_notify(call, get_alert_reply, NULL, NULL);
}

static void get_silent_reply(DBusPendingCall *call, void *user_data)
{
	DBusError derr;
	DBusMessage *reply;
	DBusMessageIter iter;
	const char *setting = NULL;

	reply = dbus_pending_call_steal_reply(call);

	dbus_error_init(&derr);
	if (dbus_set_error_from_message(&derr, reply)) {
		error("D-Bus replied with error: %s, %s", derr.name,
								derr.message);
		dbus_error_free(&derr);
		goto done;
	}

	dbus_message_iter_init(reply, &iter);
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
		error("Unexpected signature for reply");
		goto done;
	}

	dbus_message_iter_get_basic(&iter, &setting);

	DBG("Ringer Setting: %s", setting);

	if (g_str_equal(setting, "Silent"))
		ringer_setting = RINGER_SILENT;
	else
		ringer_setting = RINGER_NORMAL;

done:
	dbus_message_unref(reply);
	dbus_pending_call_unref(call);
}

static void get_silent_mode(DBusConnection *conn, void *user_data)
{
	DBusMessage *msg;
	DBusPendingCall *call;

	if (!agent.name) {
		error("Agent not registered");
		return;
	}

	DBG("Get Silent Mode: agent %s, %s", agent.name, agent.path);

	msg = dbus_message_new_method_call(agent.name, agent.path,
					AGENT_INTERFACE, "GetSilentMode");
	if (msg == NULL) {
		error("Unable to allocate new D-Bus message");
		return;
	}

	if (!dbus_connection_send_with_reply(connection, msg, &call, -1)) {
		error("Failed to send D-Bus message");
		return;
	}

	dbus_pending_call_set_notify(call, get_silent_reply, NULL, NULL);
}

static uint8_t alert_status_read(struct attribute *a, gpointer user_data)
{
	if (alert_status == 0xff) {
		get_alert_status(connection, NULL);
		return ATT_ECODE_IO;
	}

	DBG("a = %p, state = %s", a,
		alert_status & ALERT_RINGER_STATE ? "Active": "Not Active");

	if (a->data == NULL || a->data[0] != alert_status)
		attrib_db_update(a->handle, NULL, &alert_status,
						sizeof(alert_status), NULL);

	return 0;
}

static uint8_t ringer_setting_read(struct attribute *a, gpointer user_data)
{
	if (ringer_setting == 0xff) {
		get_silent_mode(connection, NULL);
		return ATT_ECODE_IO;
	}

	DBG("a = %p, setting = %s", a,
			ringer_setting == RINGER_SILENT ? "Silent": "Normal");

	if (a->data == NULL || a->data[0] != ringer_setting)
		attrib_db_update(a->handle, NULL, &ringer_setting,
						sizeof(ringer_setting), NULL);

	return 0;
}

static void register_phone_alert_service(void)
{
	/* Phone Alert Status Service */
	gatt_service_add(GATT_PRIM_SVC_UUID, PHONE_ALERT_STATUS_SVC_UUID,
			/* Alert Status characteristic */
			GATT_OPT_CHR_UUID, ALERT_STATUS_CHR_UUID,
			GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_READ |
							ATT_CHAR_PROPER_NOTIFY,
			GATT_OPT_CHR_VALUE_CB, ATTRIB_READ,
			alert_status_read,
			GATT_OPT_CHR_VALUE_GET_HANDLE, &handle_alert_status,
			/* Ringer Control Point characteristic */
			GATT_OPT_CHR_UUID, RINGER_CP_CHR_UUID,
			GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_WRITE,
			GATT_OPT_CHR_VALUE_CB, ATTRIB_WRITE,
			control_point_write,
			/* Ringer Setting characteristic */
			GATT_OPT_CHR_UUID, RINGER_SETTING_CHR_UUID,
			GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_READ |
							ATT_CHAR_PROPER_NOTIFY,
			GATT_OPT_CHR_VALUE_CB, ATTRIB_READ,
			ringer_setting_read,
			GATT_OPT_CHR_VALUE_GET_HANDLE, &handle_ringer_setting,
			GATT_OPT_INVALID);
}

static void agent_exited(DBusConnection *conn, void *user_data)
{
	DBG("Agent exiting ...");

	g_free(agent.path);
	g_free(agent.name);

	agent.path = NULL;
	agent.name = NULL;
}

static DBusMessage *register_agent(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	const char *path, *name;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &path,
							DBUS_TYPE_INVALID))
		return NULL;

	if (agent.name != NULL)
		return btd_error_already_exists(msg);

	name = dbus_message_get_sender(msg);

	DBG("Registering agent: path = %s, name = %s", path, name);

	agent.path = strdup(path);
	agent.name = strdup(name);

	agent.listener_id = g_dbus_add_disconnect_watch(connection, name,
							agent_exited, NULL,
									NULL);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *notify_ringer_setting(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	const char *setting;

	if (agent.name == NULL)
		return NULL;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &setting,
							DBUS_TYPE_INVALID))
		return NULL;

	if (g_str_equal(setting, "Silent"))
		ringer_setting = RINGER_SILENT;
	else
		ringer_setting = RINGER_NORMAL;

	attrib_db_update(handle_ringer_setting, NULL, &ringer_setting,
						sizeof(ringer_setting), NULL);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *notify_alert_status(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	uint8_t status;

	if (agent.name == NULL)
		return NULL;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_BYTE, &status,
							DBUS_TYPE_INVALID))
		return NULL;

	alert_status = status;

	attrib_db_update(handle_alert_status, NULL, &alert_status,
						sizeof(alert_status), NULL);

	return dbus_message_new_method_return(msg);
}

static GDBusMethodTable alert_methods[] = {
	{ "RegisterAgent",	"o",	"",	register_agent		},
	{ "NotifyRingerSetting","s",	"",	notify_ringer_setting	},
	{ "NotifyAlertStatus","y",	"",	notify_alert_status	},
	{ }
};

int alert_server_init(void)
{
	connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	if (connection == NULL)
		return -EIO;

	if (!g_dbus_register_interface(connection, ALERT_PATH, ALERT_INTERFACE,
				alert_methods, NULL, NULL,
				NULL, NULL) == TRUE) {
		error("D-Bus failed to register %s interface", ALERT_INTERFACE);
		dbus_connection_unref(connection);
		connection = NULL;

		return -1;
	}

	DBG("Registered interface %s on path %s", ALERT_INTERFACE, ALERT_PATH);

	register_phone_alert_service();

	return 0;
}

void alert_server_exit(void)
{
	dbus_connection_unref(connection);
	connection = NULL;
}
