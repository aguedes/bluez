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

#include <glib.h>
#include <errno.h>
#include <bluetooth/uuid.h>
#include <stdbool.h>

#include "adapter.h"
#include "device.h"
#include "profile.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"
#include "battery.h"
#include "manager.h"

static int battery_driver_probe(struct btd_profile *p,
						struct btd_device *device,
						GSList *uuids)
{
	return battery_register(device);
}

static void battery_driver_remove(struct btd_profile *p,
						struct btd_device *device)
{
	battery_unregister(device);
}

static struct btd_profile battery_profile = {
	.name		= "battery",
	.remote_uuids	= BTD_UUIDS(BATTERY_SERVICE_UUID),
	.device_probe	= battery_driver_probe,
	.device_remove	= battery_driver_remove
};

int battery_manager_init(void)
{
	return btd_profile_register(&battery_profile);
}

void battery_manager_exit(void)
{
	btd_profile_unregister(&battery_profile);
}
