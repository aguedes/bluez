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

#include <errno.h>

#include <glib.h>

#include "system.h"

static GSList *system_ops_list = NULL;

struct system_ops *system_ops = NULL;

int btd_system_lock(void)
{
	if (system_ops == NULL)
		return -EINVAL;

	return system_ops->lock();
}

int btd_system_unlock(void)
{
	if (system_ops == NULL)
		return -EINVAL;

	return system_ops->unlock();
}

int btd_system_enable_alert(int interval, gboolean vibrate)
{
	if (system_ops == NULL)
		return -EINVAL;

	return 0;
}

void btd_system_disable_alert(void)
{
	if (system_ops == NULL)
		return;
}

int btd_register_system_ops(struct system_ops *ops, gboolean priority)
{
	if (priority)
		system_ops_list = g_slist_prepend(system_ops_list, ops);
	else
		system_ops_list = g_slist_append(system_ops_list, ops);

	/* First entry has higher priority */
	system_ops = system_ops_list->data;

	return 0;
}

void btd_unregister_system_ops(struct system_ops *ops)
{
	system_ops_list = g_slist_remove(system_ops_list, ops);

	system_ops = (system_ops_list ? system_ops_list->data : NULL);
}
