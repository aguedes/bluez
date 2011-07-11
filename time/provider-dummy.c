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

#include <stdint.h>

#include "server.h"
#include "log.h"

int time_provider_init(void)
{
	DBG("initializing dummy time provider");

	return 0;
}


void time_provider_exit(void)
{
	DBG("exiting dummy time provider");
}

void time_provider_status(uint8_t *state, uint8_t *result)
{
	*state = UPDATE_STATE_IDLE;
	*result = UPDATE_RESULT_NOT_ATTEMPTED;
}

uint8_t time_provider_control(int op)
{
	DBG("dummy time provider: updating time");

	return 0;
}
