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
#include <bluetooth/uuid.h>

#include "att.h"
#include "gattrib.h"
#include "attrib-server.h"
#include "gatt-service.h"
#include "log.h"
#include "server.h"

#define PHONE_ALERT_STATUS_SVC_UUID		0x180E

#define RINGER_CP_CHR_UUID		0x2A40
#define RINGER_SETTING_CHR_UUID		0x2A41

static uint8_t control_point_write(struct attribute *a, gpointer user_data)
{
	DBG("a = %p", a);

	return 0;
}

static uint8_t ringer_setting_read(struct attribute *a, gpointer user_data)
{
	DBG("a = %p", a);

	return 0;
}

static void register_phone_alert_service(void)
{
	/* Phone Alert Status Service */
	gatt_service_add(GATT_PRIM_SVC_UUID, PHONE_ALERT_STATUS_SVC_UUID,
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
			GATT_OPT_INVALID);
}

int alert_server_init(void)
{
	register_phone_alert_service();

	return 0;
}

void alert_server_exit(void)
{
}
