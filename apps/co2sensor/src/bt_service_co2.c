#include <errno.h>
#include <init.h>
#include <stddef.h>
#include <string.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <zephyr/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>

#include "main.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(co2_gatt, LOG_LEVEL_DBG);

#define BT_UUID_CO2 \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000001, 0xa05a, 0x40f0, 0x8ff3, 0x3a5320959b49))
#define BT_UUID_CO2_METERSTATUS \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000002, 0xa05a, 0x40f0, 0x8ff3, 0x3a5320959b49))
#define BT_UUID_CO2_ALARMSTATUS \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000003, 0xa05a, 0x40f0, 0x8ff3, 0x3a5320959b49))
#define BT_UUID_CO2_OUTPUTSTATUS \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000004, 0xa05a, 0x40f0, 0x8ff3, 0x3a5320959b49))
#define BT_UUID_CO2_SPACECO2 \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000005, 0xa05a, 0x40f0, 0x8ff3, 0x3a5320959b49))

static ssize_t meterstatus_read(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf,
				uint16_t len,
				uint16_t offset)
{
	uint8_t data[2];

	sys_put_le16(g_main_meterstatus, data);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

static ssize_t alarmstatus_read(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf,
				uint16_t len,
				uint16_t offset)
{
	uint8_t data[2];

	sys_put_le16(g_main_alarmstatus, data);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

static ssize_t outputstatus_read(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 void *buf,
				 uint16_t len,
				 uint16_t offset)
{
	uint8_t data[2];

	sys_put_le16(g_main_outputstatus, data);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

static ssize_t spaceco2_read(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf,
			     uint16_t len,
			     uint16_t offset)
{
	uint8_t data[2];

	sys_put_le16(g_main_spaceco2, data);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

BT_GATT_SERVICE_DEFINE(dehumid_svc,
		       BT_GATT_PRIMARY_SERVICE(BT_UUID_CO2),

		       BT_GATT_CHARACTERISTIC(BT_UUID_CO2_METERSTATUS,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ_ENCRYPT,
					      meterstatus_read,
					      NULL,
					      NULL),
		       BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

		       BT_GATT_CHARACTERISTIC(BT_UUID_CO2_ALARMSTATUS,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ_ENCRYPT,
					      alarmstatus_read,
					      NULL,
					      NULL),
		       BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

		       BT_GATT_CHARACTERISTIC(BT_UUID_CO2_OUTPUTSTATUS,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ_ENCRYPT,
					      outputstatus_read,
					      NULL,
					      NULL),
		       BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

		       BT_GATT_CHARACTERISTIC(BT_UUID_CO2_SPACECO2,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ_ENCRYPT,
					      spaceco2_read,
					      NULL,
					      NULL),
		       BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), );

int bt_co2_meterstatus_notify(uint16_t val)
{
	int rc;
	uint8_t buf[2];

	sys_put_le16(val, buf);

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[1], buf, sizeof(buf));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_co2_alarmstatus_notify(uint16_t val)
{
	int rc;
	uint8_t buf[2];

	sys_put_le16(val, buf);

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[4], buf, sizeof(buf));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_co2_outputstatus_notify(uint16_t val)
{
	int rc;
	uint8_t buf[2];

	sys_put_le16(val, buf);

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[7], buf, sizeof(buf));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_co2_spaceco2_notify(uint16_t val)
{
	int rc;
	uint8_t buf[2];

	sys_put_le16(val, buf);

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[10], buf, sizeof(buf));

	return rc == -ENOTCONN ? 0 : rc;
}
