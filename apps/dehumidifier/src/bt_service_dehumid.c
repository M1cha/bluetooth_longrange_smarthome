#include <errno.h>
#include <init.h>
#include <stddef.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>

#include "main.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(dehumid_gatt, LOG_LEVEL_DBG);

#define BT_UUID_DEHUMID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000001, 0xb28b, 0x44f9, 0xa91a, 0x5c7c674ba354))
#define BT_UUID_DEHUMID_IONIZER \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000002, 0xb28b, 0x44f9, 0xa91a, 0x5c7c674ba354))
#define BT_UUID_DEHUMID_FAN \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000003, 0xb28b, 0x44f9, 0xa91a, 0x5c7c674ba354))
#define BT_UUID_DEHUMID_COMPRESSOR \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000004, 0xb28b, 0x44f9, 0xa91a, 0x5c7c674ba354))
#define BT_UUID_DEHUMID_WATERBOX \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00000005, 0xb28b, 0x44f9, 0xa91a, 0x5c7c674ba354))

static ssize_t ionizer_read(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf,
			    uint16_t len,
			    uint16_t offset)
{
	bool on;
	uint8_t val;
	int rc;

	rc = main_ionizer_get(&on);
	if (rc) {
		return rc;
	}
	val = on;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

static ssize_t ionizer_write(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf_,
			     uint16_t len,
			     uint16_t offset,
			     uint8_t flags)
{
	const uint8_t *buf = buf_;
	int ret;

	if (!buf || len != 1 || offset != 0) {
		return -ENOTSUP;
	}

	ret = main_ionizer_set(buf[0]);
	if (ret) {
		return ret;
	}

	return len;
}

static ssize_t fan_read(struct bt_conn *conn,
			const struct bt_gatt_attr *attr,
			void *buf,
			uint16_t len,
			uint16_t offset)
{
	enum fanmode mode;
	int rc;
	uint8_t val;

	rc = main_fan_get(&mode);
	if (rc) {
		return rc;
	}

	val = mode;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

static ssize_t fan_write(struct bt_conn *conn,
			 const struct bt_gatt_attr *attr,
			 const void *buf_,
			 uint16_t len,
			 uint16_t offset,
			 uint8_t flags)
{
	const uint8_t *buf = buf_;
	int ret;

	if (!buf || len != 1 || offset != 0) {
		return -ENOTSUP;
	}

	uint8_t mode = buf[0];
	switch (mode) {
	case FANMODE_OFF:
	case FANMODE_HALF:
	case FANMODE_FULL:
		ret = main_fan_set(mode);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	if (ret) {
		return ret;
	}

	return len;
}

static ssize_t compressor_read(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf,
			       uint16_t len,
			       uint16_t offset)
{
	bool on;
	uint8_t val;
	int rc;

	rc = main_compressor_get(&on);
	if (rc) {
		return rc;
	}
	val = on;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

static ssize_t compressor_write(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf_,
				uint16_t len,
				uint16_t offset,
				uint8_t flags)
{
	const uint8_t *buf = buf_;
	int ret;

	if (!buf || len != 1 || offset != 0) {
		return -ENOTSUP;
	}

	ret = main_compressor_set(buf[0]);
	if (ret) {
		return ret;
	}

	return len;
}

static ssize_t waterbox_read(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf,
			     uint16_t len,
			     uint16_t offset)
{
	bool on;
	uint8_t val;
	int rc;

	rc = main_waterbox_get(&on);
	if (rc) {
		return rc;
	}
	val = on;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

BT_GATT_SERVICE_DEFINE(
	dehumid_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DEHUMID),

	BT_GATT_CHARACTERISTIC(BT_UUID_DEHUMID_IONIZER,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       ionizer_read,
			       ionizer_write,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

	BT_GATT_CHARACTERISTIC(BT_UUID_DEHUMID_FAN,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       fan_read,
			       fan_write,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

	BT_GATT_CHARACTERISTIC(BT_UUID_DEHUMID_COMPRESSOR,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       compressor_read,
			       compressor_write,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

	BT_GATT_CHARACTERISTIC(BT_UUID_DEHUMID_WATERBOX,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT,
			       waterbox_read,
			       NULL,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), );

int bt_dehumid_ionizer_notify(bool val_)
{
	int rc;
	uint8_t val = val_;

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[1], &val, sizeof(val));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_dehumid_fan_notify(enum fanmode mode)
{
	int rc;
	uint8_t val = mode;

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[4], &val, sizeof(val));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_dehumid_compressor_notify(bool val_)
{
	int rc;
	uint8_t val = val_;

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[7], &val, sizeof(val));

	return rc == -ENOTCONN ? 0 : rc;
}

int bt_dehumid_waterbox_notify(bool val_)
{
	int rc;
	uint8_t val = val_;

	rc = bt_gatt_notify(NULL, &dehumid_svc.attrs[10], &val, sizeof(val));

	return rc == -ENOTCONN ? 0 : rc;
}
