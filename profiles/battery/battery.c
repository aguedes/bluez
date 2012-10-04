/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012 Texas Instruments, Inc.
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
#include <bluetooth/uuid.h>
#include <stdbool.h>
#include <sys/file.h>
#include <stdlib.h>

#include "adapter.h"
#include "device.h"
#include "gattrib.h"
#include "attio.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"
#include "battery.h"
#include "log.h"
#include "storage.h"

#define BATTERY_KEY_FORMAT	"%17s#%04X"
#define BATTERY_LEVEL_FORMAT	"%03d"

struct battery {
	struct btd_device	*dev;		/* Device reference */
	GAttrib			*attrib;	/* GATT connection */
	guint			attioid;	/* Att watcher id */
	struct att_range	*svc_range;	/* Battery range */
	guint                   attnotid;       /* Att notifications id */
	GSList			*chars;		/* Characteristics */
};

static GSList *servers;

struct characteristic {
	struct btd_battery	*devbatt;	/* device_battery pointer */
	struct gatt_char	attr;		/* Characteristic */
	struct battery		*batt;		/* Parent Battery Service */
	GSList			*desc;		/* Descriptors */
	uint8_t			ns;		/* Battery Namespace */
	uint16_t		description;	/* Battery description */
	uint8_t			level;		/* Battery level */
	gboolean		can_notify;	/* Char can notify flag */
};

struct descriptor {
	struct characteristic	*ch;		/* Parent Characteristic */
	uint16_t		handle;		/* Descriptor Handle */
	bt_uuid_t		uuid;		/* UUID */
};

static gint cmp_device(gconstpointer a, gconstpointer b)
{
	const struct battery *batt = a;
	const struct btd_device *dev = b;

	if (dev == batt->dev)
		return 0;

	return -1;
}

static inline int create_filename(char *buf, size_t size,
				const bdaddr_t *bdaddr, const char *name)
{
	char addr[18];

	ba2str(bdaddr, addr);

	return create_name(buf, size, STORAGEDIR, addr, name);
}

static int store_battery_char(struct characteristic *chr)
{
	char filename[PATH_MAX + 1], addr[18], key[23];
	bdaddr_t sba, dba;
	char level[4];

	adapter_get_address(device_get_adapter(chr->batt->dev), &sba);
	device_get_address(chr->batt->dev, &dba, NULL);

	create_filename(filename, PATH_MAX, &sba, "battery_gatt_client");

	create_file(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	ba2str(&dba, addr);

	snprintf(key, sizeof(key), BATTERY_KEY_FORMAT, addr, chr->attr.handle);
	snprintf(level, sizeof(level), BATTERY_LEVEL_FORMAT, chr->level);

	return textfile_caseput(filename, key, level);
}

static char *read_battery_char(struct characteristic *chr)
{
	char filename[PATH_MAX + 1], addr[18], key[23];
	char *str, *strnew;
	bdaddr_t sba, dba;

	adapter_get_address(device_get_adapter(chr->batt->dev), &sba);
	device_get_address(chr->batt->dev, &dba, NULL);

	create_filename(filename, PATH_MAX, &sba, "battery_gatt_client");

	ba2str(&dba, addr);
	snprintf(key, sizeof(key), BATTERY_KEY_FORMAT, addr, chr->attr.handle);

	str = textfile_caseget(filename, key);
	if (str == NULL)
		return NULL;

	strnew = g_strdup(str);
	g_free(str);

	return strnew;
}

static void del_battery_char(struct characteristic *chr)
{
	char filename[PATH_MAX + 1], addr[18], key[23];
	bdaddr_t sba, dba;

	adapter_get_address(device_get_adapter(chr->batt->dev), &sba);
	device_get_address(chr->batt->dev, &dba, NULL);

	create_filename(filename, PATH_MAX, &sba, "battery_gatt_client");

	ba2str(&dba, addr);
	snprintf(key, sizeof(key), BATTERY_KEY_FORMAT, addr, chr->attr.handle);

	textfile_casedel(filename, key);
}

static gboolean read_battery_level_value(struct characteristic *chr)
{
	char *str;

	if (!chr)
		return FALSE;

	str = read_battery_char(chr);
	if (!str)
		return FALSE;

	chr->level = atoi(str);

	btd_device_set_battery_opt(chr->devbatt, BATTERY_OPT_LEVEL, chr->level,
						BATTERY_OPT_INVALID);

	g_free(str);
	return TRUE;
}

static void char_free(gpointer user_data)
{
	struct characteristic *c = user_data;

	del_battery_char(c);

	g_slist_free_full(c->desc, g_free);

	btd_device_remove_battery(c->devbatt);

	g_free(c);
}

static gint cmp_char_val_handle(gconstpointer a, gconstpointer b)
{
	const struct characteristic *ch = a;
	const uint16_t *handle = b;

	return ch->attr.value_handle - *handle;
}

static void battery_free(gpointer user_data)
{
	struct battery *batt = user_data;

	if (batt->chars != NULL)
		g_slist_free_full(batt->chars, char_free);

	if (batt->attioid > 0)
		btd_device_remove_attio_callback(batt->dev, batt->attioid);

	if (batt->attrib != NULL) {
		if (batt->attnotid) {
			g_attrib_unregister(batt->attrib, batt->attnotid);
			batt->attnotid = 0;
		}

		g_attrib_unref(batt->attrib);
	}

	btd_device_unref(batt->dev);
	g_free(batt->svc_range);
	g_free(batt);
}

static void read_batterylevel_cb(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct characteristic *ch = user_data;
	uint8_t value[ATT_MAX_MTU];
	int vlen;

	if (status != 0) {
		error("Failed to read Battery Level:%s", att_ecode2str(status));
		return;
	}

	vlen = dec_read_resp(pdu, len, value, sizeof(value));
	if (vlen < 0) {
		error("Failed to read Battery Level: Protocol error");
		return;
	}

	if (vlen != 1) {
		error("Failed to read Battery Level: Wrong pdu len");
		return;
	}

	ch->level = value[0];
	btd_device_set_battery_opt(ch->devbatt, BATTERY_OPT_LEVEL, ch->level,
						BATTERY_OPT_INVALID);

	store_battery_char(ch);
}

static void process_batteryservice_char(struct characteristic *ch)
{
	if (g_strcmp0(ch->attr.uuid, BATTERY_LEVEL_UUID) == 0) {
		gatt_read_char(ch->batt->attrib, ch->attr.value_handle,
						read_batterylevel_cb, ch);
	}
}

static void batterylevel_enable_notify_cb(guint8 status, const guint8 *pdu,
						guint16 len, gpointer user_data)
{
	struct characteristic *ch = user_data;

	if (status != 0) {
		error("Could not enable batt level notification.");
		ch->can_notify = FALSE;
		process_batteryservice_char(ch);
	}
}

static gint device_battery_cmp(gconstpointer a, gconstpointer b)
{
	const struct characteristic *ch = a;
	const struct btd_battery *batt = b;

	if (batt == ch->devbatt)
		return 0;

	return -1;
}

static struct characteristic *find_battery_char(struct btd_battery *db)
{
	GSList *l, *b;

	for (l = servers; l != NULL; l = g_slist_next(l)) {
		struct battery *batt = l->data;

		b = g_slist_find_custom(batt->chars, db, device_battery_cmp);
		if (b)
			return b->data;
	}

	return NULL;
}

static void batterylevel_refresh_cb(struct btd_battery *batt)
{
	struct characteristic *ch;

	ch = find_battery_char(batt);

	if (ch)
		process_batteryservice_char(ch);
}

static void enable_battery_notification(struct characteristic *ch,
								uint16_t handle)
{
	uint8_t atval[2];
	uint16_t val;

	val = GATT_CLIENT_CHARAC_CFG_NOTIF_BIT;

	ch->can_notify = TRUE;

	att_put_u16(val, atval);
	gatt_write_char(ch->batt->attrib, handle, atval, 2,
				batterylevel_enable_notify_cb, ch);
}

static void batterylevel_presentation_format_desc_cb(guint8 status,
						const guint8 *pdu, guint16 len,
						gpointer user_data)
{
	struct descriptor *desc = user_data;
	uint8_t value[ATT_MAX_MTU];
	int vlen;

	if (status != 0) {
		error("Presentation Format desc read failed: %s",
							att_ecode2str(status));
		return;
	}

	vlen = dec_read_resp(pdu, len, value, sizeof(value));
	if (vlen < 0) {
		error("Presentation Format desc read failed: Protocol error");
		return;
	}

	if (vlen < 7) {
		error("Presentation Format desc read failed: Invalid range");
		return;
	}

	desc->ch->ns = value[4];
	desc->ch->description = att_get_u16(&value[5]);
}

static void process_batterylevel_desc(struct descriptor *desc)
{
	struct characteristic *ch = desc->ch;
	char uuidstr[MAX_LEN_UUID_STR];
	bt_uuid_t btuuid;

	bt_uuid16_create(&btuuid, GATT_CLIENT_CHARAC_CFG_UUID);

	if (bt_uuid_cmp(&desc->uuid, &btuuid) == 0 && g_strcmp0(ch->attr.uuid,
						BATTERY_LEVEL_UUID) == 0) {
		enable_battery_notification(ch, desc->handle);
		return;
	}

	bt_uuid16_create(&btuuid, GATT_CHARAC_FMT_UUID);

	if (bt_uuid_cmp(&desc->uuid, &btuuid) == 0) {
		gatt_read_char(ch->batt->attrib, desc->handle,
				batterylevel_presentation_format_desc_cb, desc);
		return;
	}

	bt_uuid_to_string(&desc->uuid, uuidstr, MAX_LEN_UUID_STR);
	DBG("Ignored descriptor %s characteristic %s", uuidstr,	ch->attr.uuid);
}

static void discover_desc_cb(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct characteristic *ch = user_data;
	struct att_data_list *list;
	uint8_t format;
	int i;

	if (status != 0) {
		error("Discover all characteristic descriptors failed [%s]: %s",
					ch->attr.uuid, att_ecode2str(status));
		return;
	}

	list = dec_find_info_resp(pdu, len, &format);
	if (list == NULL)
		return;

	for (i = 0; i < list->num; i++) {
		struct descriptor *desc;
		uint8_t *value;

		value = list->data[i];
		desc = g_new0(struct descriptor, 1);
		desc->handle = att_get_u16(value);
		desc->ch = ch;

		if (format == 0x01)
			desc->uuid = att_get_uuid16(&value[2]);
		else
			desc->uuid = att_get_uuid128(&value[2]);

		ch->desc = g_slist_append(ch->desc, desc);
		process_batterylevel_desc(desc);
	}

	att_data_list_free(list);
}

static void configure_battery_cb(GSList *characteristics, guint8 status,
							gpointer user_data)
{
	struct battery *batt = user_data;
	GSList *l;

	if (status != 0) {
		error("Discover Battery characteristics: %s",
							att_ecode2str(status));
		return;
	}

	for (l = characteristics; l; l = l->next) {
		struct gatt_char *c = l->data;
		struct characteristic *ch;
		uint16_t start, end;

		if (g_strcmp0(c->uuid, BATTERY_LEVEL_UUID) != 0)
			continue;

		ch = g_new0(struct characteristic, 1);
		ch->attr.handle = c->handle;
		ch->attr.properties = c->properties;
		ch->attr.value_handle = c->value_handle;
		memcpy(ch->attr.uuid, c->uuid, MAX_LEN_UUID_STR + 1);
		ch->batt = batt;

		batt->chars = g_slist_append(batt->chars, ch);

		start = c->value_handle + 1;

		if (!read_battery_level_value(ch))
			process_batteryservice_char(ch);

		ch->devbatt = btd_device_add_battery(ch->batt->dev);

		btd_device_set_battery_opt(ch->devbatt,
						   BATTERY_OPT_REFRESH_FUNC,
						   batterylevel_refresh_cb,
						   BATTERY_OPT_INVALID);

		if (l->next != NULL) {
			struct gatt_char *c = l->next->data;
			if (start == c->handle)
				continue;
			end = c->handle - 1;
		} else if (c->value_handle != batt->svc_range->end)
			end = batt->svc_range->end;
		else
			continue;

		gatt_find_info(batt->attrib, start, end, discover_desc_cb, ch);
	}
}

static void proc_batterylevel(struct characteristic *c, const uint8_t *pdu,
						uint16_t len, gboolean final)
{
	if (!pdu) {
		error("Battery level notification: Invalid pdu length");
		return;
	}

	c->level = pdu[1];

	btd_device_set_battery_opt(c->devbatt, BATTERY_OPT_LEVEL, c->level,
							BATTERY_OPT_INVALID);
}

static void notif_handler(const uint8_t *pdu, uint16_t len, gpointer user_data)
{
	struct battery *batt = user_data;
	struct characteristic *ch;
	uint16_t handle;
	GSList *l;

	if (len < 3) {
		error("notif_handler: Bad pdu received");
		return;
	}

	handle = att_get_u16(&pdu[1]);
	l = g_slist_find_custom(batt->chars, &handle, cmp_char_val_handle);
	if (l == NULL) {
		error("notif_handler: Unexpected handle 0x%04x", handle);
		return;
	}

	ch = l->data;
	if (g_strcmp0(ch->attr.uuid, BATTERY_LEVEL_UUID) == 0) {
		proc_batterylevel(ch, pdu, len, FALSE);
	}
}

static void attio_connected_cb(GAttrib *attrib, gpointer user_data)
{
	struct battery *batt = user_data;

	batt->attrib = g_attrib_ref(attrib);

	batt->attnotid = g_attrib_register(batt->attrib,
						ATT_OP_HANDLE_NOTIFY,
						GATTRIB_ALL_HANDLES,
						notif_handler, batt, NULL);

	if (batt->chars == NULL) {
		gatt_discover_char(batt->attrib, batt->svc_range->start,
					batt->svc_range->end, NULL,
					configure_battery_cb, batt);
	} else {
		GSList *l;
		for (l = batt->chars; l; l = l->next) {
			struct characteristic *c = l->data;
			if (!read_battery_level_value(c) && !c->can_notify)
				process_batteryservice_char(c);
		}
	}
}

static void attio_disconnected_cb(gpointer user_data)
{
	struct battery *batt = user_data;

	g_attrib_unregister(batt->attrib, batt->attnotid);
	batt->attnotid = 0;
	g_attrib_unref(batt->attrib);
	batt->attrib = NULL;
}

static gint primary_uuid_cmp(gconstpointer a, gconstpointer b)
{
	const struct gatt_primary *prim = a;
	const char *uuid = b;

	return g_strcmp0(prim->uuid, uuid);
}

int battery_register(struct btd_device *device)
{
	struct battery *batt;
	struct gatt_primary *prim;
	GSList *primaries, *l;

	primaries = btd_device_get_primaries(device);

	l = g_slist_find_custom(primaries, BATTERY_SERVICE_UUID,
							primary_uuid_cmp);
	prim = l->data;

	batt = g_new0(struct battery, 1);
	batt->dev = btd_device_ref(device);

	batt->svc_range = g_new0(struct att_range, 1);
	batt->svc_range->start = prim->range.start;
	batt->svc_range->end = prim->range.end;

	servers = g_slist_prepend(servers, batt);

	batt->attioid = btd_device_add_attio_callback(device,
				attio_connected_cb, attio_disconnected_cb,
				batt);
	return 0;
}

void battery_unregister(struct btd_device *device)
{
	struct battery *batt;
	GSList *l;

	l = g_slist_find_custom(servers, device, cmp_device);
	if (l == NULL)
		return;

	batt = l->data;
	servers = g_slist_remove(servers, batt);

	battery_free(batt);
}
