/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2013  Instituto Nokia de Tecnologia - INdT
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

struct btd_attribute;

void btd_gatt_service_manager_init(void);

void btd_gatt_service_manager_cleanup(void);

struct btd_attribute *btd_gatt_add_service(bt_uuid_t type, bool is_primary);

void btd_gatt_remove_service(struct btd_attribute *service);

/* Returns a reference to characteristic value attribute */
struct btd_attribute *btd_gatt_add_char(bt_uuid_t type,
						btd_attr_read_t read_cb,
						btd_attr_write_t write_cb);

void btd_gatt_add_char_desc(bt_uuid_t type, btd_attr_read_t read_cb,
				btd_attr_write_t write_cb);

/* The caller must free the list returned */
GSList *btd_gatt_get_services(struct btd_service *service);

/* The caller must free the list returned */
GSList *btd_gatt_get_chars_decl(struct btd_attribute *service,
					bt_uuid_t type);

/* The caller must free the list returned */
struct btd_attribute *btd_gatt_get_char_desc(struct btd_attribute *chr,
						bt_uuid_t type);

struct btd_attribute *btd_gatt_get_char_value(struct btd_attribute *chr);

void btd_gatt_read_attribute(struct attribute *attr,
				btd_attr_read_result_t result, void *user_data);

void btd_gatt_write_attribute(struct attribute *attr, uint8_t *value,
				size_t len, uint16_t offset,
				btd_attr_write_result_t result,
				void *user_data);

unsigned int btd_gatt_add_notifier(struct attribute *attr,
					btd_attr_value_t value_cb,
					void *user_data);

void btd_gatt_remove_notifier(struct attribute *attr, unsigned int id);
