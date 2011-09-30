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
#include <stdlib.h>
#include <bluetooth/uuid.h>

#include "adapter.h"
#include "manager.h"

#include "att.h"
#include "gattrib.h"
#include "attio.h"
#include "textfile.h"
#include "storage.h"
#include "attrib-server.h"
#include "gatt-service.h"
#include "log.h"
#include "server.h"

#define CURRENT_TIME_SVC_UUID		0x1805
#define REF_TIME_UPDATE_SVC_UUID	0x1806

#define LOCAL_TIME_INFO_CHR_UUID	0x2A0F
#define TIME_UPDATE_CTRL_CHR_UUID	0x2A16
#define TIME_UPDATE_STAT_CHR_UUID	0x2A17
#define CT_TIME_CHR_UUID		0x2A2B

struct adapter_ccc {
	struct btd_adapter *adapter;
	uint16_t handle;
	GSList *devices;
};

struct notify_callback {
	struct btd_device *device;
	guint id;
};

static uint16_t current_time_ccc_handle;
static uint16_t current_time_value_handle;

static int encode_current_time(uint8_t value[10])
{
	struct timespec tp;
	struct tm tm;

	if (clock_gettime(CLOCK_REALTIME, &tp) == -1) {
		error("clock_gettime: %s", strerror(errno));
		/* FIXME: add proper application error codes */
		return -errno;
	}

	if (localtime_r(&tp.tv_sec, &tm) == NULL) {
		error("localtime_r() failed");
		return -errno;
	}

	att_put_u16(1900 + tm.tm_year, &value[0]); /* Year */
	value[2] = tm.tm_mon + 1; /* Month */
	value[3] = tm.tm_mday; /* Day */
	value[4] = tm.tm_hour; /* Hours */
	value[5] = tm.tm_min; /* Minutes */
	value[6] = tm.tm_sec; /* Seconds */
	value[7] = tm.tm_wday == 0 ? 7 : tm.tm_wday; /* Day of Week */
	/* From Time Profile spec: "The number of 1/256 fractions of a second."
	 * In 1s there are 256 fractions, in 1ns there are 256/10^9 fractions.
	 * To avoid integer overflow, we use the equivalent 1/3906250 ratio. */
	value[8] = tp.tv_nsec / 3906250; /* Fractions256 */
	value[9] = 0x00; /* Adjust Reason */

	return 0;
}

static uint8_t current_time_read(struct attribute *a, gpointer user_data)
{
	uint8_t value[10];
	int err;

	err = encode_current_time(value);
	if (err < 0)
		return ATT_ECODE_IO;

	attrib_db_update(a->handle, NULL, value, sizeof(value), NULL);

	return 0;
}

static void filter_devices_notify(char *key, char *value, void *user_data)
{
	struct adapter_ccc *ccc = user_data;
	struct btd_adapter *adapter = ccc->adapter;
	struct btd_device *device;
	char addr[18];
	uint16_t handle, ccc_val;

	sscanf(key, "%17s#%04hX", addr, &handle);

	if (ccc->handle != handle)
		return;

	ccc_val = strtol(value, NULL, 16);
	if (!(ccc_val & 0x0001))
		return;

	device = adapter_find_device(adapter, addr);
	if (device == NULL)
		return;

	ccc->devices = g_slist_append(ccc->devices, device);
}

static GSList *devices_to_notify(struct btd_adapter *adapter, uint16_t ccc_hnd)
{
	struct adapter_ccc ccc_list = { adapter, ccc_hnd, NULL };
	char filename[PATH_MAX + 1];
	char srcaddr[18];
	bdaddr_t src;

	adapter_get_address(adapter, &src);
	ba2str(&src, srcaddr);

	create_name(filename, PATH_MAX, STORAGEDIR, srcaddr, "configs");

	textfile_foreach(filename, filter_devices_notify, &ccc_list);

	return ccc_list.devices;
}

static void send_notification(GAttrib *attrib, gpointer user_data)
{
	struct notify_callback *callback = user_data;
	uint8_t value[10], pdu[ATT_MAX_MTU];
	int err, len;

	err = encode_current_time(value);
	if (err)
		goto done;

	len = enc_notification(current_time_value_handle, value, sizeof(value),
							pdu, sizeof(pdu));
	g_attrib_send(attrib, 0, ATT_OP_HANDLE_NOTIFY, pdu, len,
							NULL, NULL, NULL);

done:
	btd_device_remove_attio_callback(callback->device, callback->id);
	g_free(callback);
}

void current_time_updated(void)
{
	struct btd_adapter *adapter;
	GSList *devices, *l;

	adapter = manager_get_default_adapter();

	devices = devices_to_notify(adapter, current_time_ccc_handle);

	for (l = devices; l; l = l->next) {
		struct btd_device *device = l->data;
		struct notify_callback *callback;

		callback = g_new0(struct notify_callback, 1);
		callback->device = device;

		callback->id = btd_device_add_attio_callback(device,
					send_notification, NULL, callback);
	}

	g_slist_free(devices);
}

static uint8_t local_time_info_read(struct attribute *a, gpointer user_data)
{
	uint8_t value[2];

	DBG("a=%p", a);

	tzset();

	/* FIXME: POSIX "daylight" variable only indicates whether there is DST
	 * for the local time or not. The offset is unknown. */
	value[0] = daylight ? 0xff : 0x00;

	/* Convert POSIX "timezone" (seconds West of GMT) to Time Profile
	 * format (offset from UTC in number of 15 minutes increments). */
	value[1] = (uint8_t) (-1 * timezone / (60 * 15));

	attrib_db_update(a->handle, NULL, value, sizeof(value), NULL);

	return 0;
}

static void register_current_time_service(void)
{
	/* Current Time service */
	gatt_service_add(GATT_PRIM_SVC_UUID, CURRENT_TIME_SVC_UUID,
				/* CT Time characteristic */
				GATT_OPT_CHR_UUID, CT_TIME_CHR_UUID,
				GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_READ |
							ATT_CHAR_PROPER_NOTIFY,
				GATT_OPT_CHR_VALUE_CB, ATTRIB_READ,
							current_time_read,
				GATT_OPT_CCC_GET_HANDLE,
						&current_time_ccc_handle,
				GATT_OPT_CHR_VALUE_GET_HANDLE,
						&current_time_value_handle,
				/* Local Time Information characteristic */
				GATT_OPT_CHR_UUID, LOCAL_TIME_INFO_CHR_UUID,
				GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_READ,
				GATT_OPT_CHR_VALUE_CB, ATTRIB_READ,
							local_time_info_read,

				GATT_OPT_INVALID);
}

static uint8_t time_update_control(struct attribute *a, gpointer user_data)
{
	DBG("handle 0x%04x", a->handle);

	if (a->len != 1) {
		DBG("Invalid control point value size: %d", a->len);
		return 0;
	}

	return time_provider_control(a->data[0]);
}

static uint8_t time_update_status(struct attribute *a, gpointer user_data)
{
	uint8_t value[2];

	DBG("handle 0x%04x", a->handle);

	time_provider_status(&value[0], &value[1]);

	attrib_db_update(a->handle, NULL, value, sizeof(value), NULL);

	return 0;
}

static void register_reference_time_update_service(void)
{
	/* Reference Time Update service */
	gatt_service_add(GATT_PRIM_SVC_UUID, REF_TIME_UPDATE_SVC_UUID,
				/* Time Update control point */
				GATT_OPT_CHR_UUID, TIME_UPDATE_CTRL_CHR_UUID,
				GATT_OPT_CHR_PROPS,
					ATT_CHAR_PROPER_WRITE_WITHOUT_RESP,
				GATT_OPT_CHR_VALUE_CB, ATTRIB_WRITE,
							time_update_control,

				/* Time Update status */
				GATT_OPT_CHR_UUID, TIME_UPDATE_STAT_CHR_UUID,
				GATT_OPT_CHR_PROPS, ATT_CHAR_PROPER_READ,
				GATT_OPT_CHR_VALUE_CB, ATTRIB_READ,
							time_update_status,

				GATT_OPT_INVALID);
}

int time_server_init(void)
{
	register_current_time_service();

	if (time_provider_init() < 0)
		DBG("error initializing time provider");
	else
		register_reference_time_update_service();

	return 0;
}

void time_server_exit(void)
{
	time_provider_exit();
}
