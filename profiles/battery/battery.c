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

#include "adapter.h"
#include "device.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"
#include "battery.h"

struct battery {
	struct btd_device	*dev;		/* Device reference */
};

static GSList *servers;

static gint cmp_device(gconstpointer a, gconstpointer b)
{
	const struct battery *batt = a;
	const struct btd_device *dev = b;

	if (dev == batt->dev)
		return 0;

	return -1;
}

static void battery_free(gpointer user_data)
{
	struct battery *batt = user_data;

	btd_device_unref(batt->dev);
	g_free(batt);
}


int battery_register(struct btd_device *device)
{
	struct battery *batt;

	batt = g_new0(struct battery, 1);
	batt->dev = btd_device_ref(device);

	servers = g_slist_prepend(servers, batt);

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
