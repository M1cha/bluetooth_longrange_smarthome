#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>
#include <settings/settings.h>
#include <sys/util.h>

#include "main.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main_bt, LOG_LEVEL_DBG);

static void start_scan(void);

struct conninfo {
	struct bt_conn *conn;

	struct bt_gatt_write_params write_params[5];
	uint8_t gatt_write_buf[5];

	struct bt_gatt_discover_params discover_params;
	uint16_t value_handle;
	struct bt_uuid_16 uuid;

	struct bt_gatt_subscribe_params sub_params[10];
};

static struct conninfo conns[CONFIG_BT_MAX_CONN];

static struct conninfo *conninfo_new(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(conns); i++) {
		if (!conns[i].conn) {
			return &conns[i];
		}
	}

	return NULL;
}

static void conninfo_free(struct conninfo *ci)
{
	memset(ci, 0, sizeof(*ci));
}

static struct conninfo *conninfo_find(struct bt_conn *conn)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(conns); i++) {
		if (conns[i].conn == conn) {
			return &conns[i];
		}
	}

	return NULL;
}

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data,
			   uint16_t length)
{
	char addr[BT_ADDR_STR_LEN];
	int rc;

	if (!data) {
		LOG_INF("[UNSUBSCRIBED] from %04x", params->value_handle);
		params->notify = NULL;
		return BT_GATT_ITER_STOP;
	}

	bt_addr_to_str(&bt_conn_get_dst(conn)->a, addr, sizeof(addr));

	rc = main_publish_characteristic_value(addr, params->value_handle, data, length);
	if (rc) {
		LOG_ERR("failed to publish characteristic value: %d", rc);
	}

	return BT_GATT_ITER_CONTINUE;
}

struct hasbond_ctx {
	bool found;
	const bt_addr_le_t *needle;
};

static void has_bond_cb(const struct bt_bond_info *info, void *ctx_)
{
	struct hasbond_ctx *ctx = ctx_;

	if (bt_addr_le_cmp(ctx->needle, &info->addr) == 0) {
		ctx->found = true;
	}
}

static void
device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	int err;
	char saddr[BT_ADDR_LE_STR_LEN];
	struct bt_conn_le_create_param *conn_params;
	struct conninfo *conninfo;
	struct hasbond_ctx hasbond_ctx = {
		.found = false,
		.needle = addr,
	};

	// filter for bonded devices
	bt_foreach_bond(BT_ID_DEFAULT, has_bond_cb, &hasbond_ctx);
	if (!hasbond_ctx.found) {
		LOG_INF("not bonded");
		return;
	}

	bt_addr_le_to_str(addr, saddr, sizeof(saddr));
	LOG_INF("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i",
		log_strdup(saddr),
		type,
		ad->len,
		rssi);

	err = bt_le_scan_stop();
	if (err) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}

	conninfo = conninfo_new();
	if (!conninfo) {
		LOG_ERR("failed to allocate conninfo");
		// apparently we're out of memory, don't restart scanning again.
		return;
	}

	conn_params = BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,
					      BT_GAP_SCAN_FAST_INTERVAL,
					      BT_GAP_SCAN_FAST_INTERVAL);

	err = bt_conn_le_create(addr, conn_params, BT_LE_CONN_PARAM_DEFAULT, &conninfo->conn);
	if (err) {
		LOG_ERR("Create conn failed (err %d)", err);
		start_scan();
		return;
	}

	LOG_INF("Connection pending");
}

static void start_scan(void)
{
	int err;
	const struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
		.options = BT_LE_SCAN_OPT_CODED | BT_LE_SCAN_OPT_NO_1M,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;
	struct bt_gatt_chrc *gatt_chrc;
	struct conninfo *conninfo = CONTAINER_OF(params, struct conninfo, discover_params);

	if (!attr) {
		LOG_INF("Discover complete");
		goto stop;
	}

	LOG_INF("[ATTRIBUTE] handle %u", attr->handle);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		gatt_chrc = attr->user_data;

		if (!(gatt_chrc->properties & BT_GATT_CHRC_NOTIFY)) {
			return BT_GATT_ITER_CONTINUE;
		}

		memcpy(&conninfo->uuid, BT_UUID_GATT_CCC, sizeof(conninfo->uuid));
		params->uuid = &conninfo->uuid.uuid;
		params->start_handle = attr->handle + 2;
		params->type = BT_GATT_DISCOVER_DESCRIPTOR;

		conninfo->value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, params);
		if (err) {
			LOG_ERR("Discover failed (err %d)", err);
			goto stop;
		}

		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
		struct bt_gatt_subscribe_params *subscribe_params = NULL;
		for (size_t i = 0; i < ARRAY_SIZE(conninfo->sub_params); i++) {
			if (conninfo->sub_params[i].notify == NULL) {
				subscribe_params = &conninfo->sub_params[i];
				break;
			}
		}

		if (!subscribe_params) {
			LOG_ERR("no free subscribe params");
			goto stop;
		}

		subscribe_params->value_handle = conninfo->value_handle;
		subscribe_params->notify = notify_func;
		subscribe_params->value = BT_GATT_CCC_NOTIFY;
		subscribe_params->ccc_handle = attr->handle;
		atomic_set_bit(subscribe_params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

		err = bt_gatt_subscribe(conn, subscribe_params);
		if (err && err != -EALREADY) {
			LOG_INF("Subscribe failed (err %d)", err);
			subscribe_params->notify = NULL;
		} else {
			LOG_INF("[SUBSCRIBED] to %04x", conninfo->value_handle);
		}

		params->uuid = NULL;
		params->start_handle = attr->handle + 1;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, params);
		if (err) {
			LOG_ERR("Discover failed(err %d)", err);
			goto stop;
		}

		return BT_GATT_ITER_STOP;
	}

stop:
	memset(params, 0, sizeof(*params));
	start_scan();
	return BT_GATT_ITER_STOP;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];
	char addr_nole[BT_ADDR_STR_LEN];
	struct conninfo *conninfo;

	conninfo = conninfo_find(conn);
	if (!conninfo) {
		LOG_ERR("connected with unknown connection");
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	bt_addr_to_str(&bt_conn_get_dst(conn)->a, addr_nole, sizeof(addr_nole));

	if (conn_err) {
		LOG_ERR("Failed to connect to %s (%u)", log_strdup(addr), conn_err);

		bt_conn_unref(conn);
		conninfo_free(conninfo);

		start_scan();

		return;
	}

	err = main_publish_connection_status(addr_nole, true);
	if (err) {
		LOG_ERR("Failed to publish connection status: %d", err);
	}

	err = bt_conn_get_info(conn, &info);
	if (err) {
		LOG_ERR("Failed to get connection info");
	} else {
		const struct bt_conn_le_phy_info *phy_info;

		phy_info = info.le.phy;
		LOG_INF("Connected: %s, tx_phy %u, rx_phy %u",
			log_strdup(addr),
			phy_info->tx_phy,
			phy_info->rx_phy);
	}

	if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
		LOG_ERR("Failed to set security");
	}

	conninfo->discover_params.uuid = NULL;
	conninfo->discover_params.func = discover_func;
	conninfo->discover_params.start_handle = BT_ATT_FIRST_ATTTRIBUTE_HANDLE;
	conninfo->discover_params.end_handle = BT_ATT_LAST_ATTTRIBUTE_HANDLE;
	conninfo->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(conn, &conninfo->discover_params);
	if (err) {
		LOG_ERR("Discover failed(err %d)", err);
		start_scan();
		return;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	char addr_nole[BT_ADDR_STR_LEN];
	struct conninfo *conninfo;
	int err;

	conninfo = conninfo_find(conn);
	if (!conninfo) {
		LOG_ERR("disconnected with unknown connection");
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	bt_addr_to_str(&bt_conn_get_dst(conn)->a, addr_nole, sizeof(addr_nole));

	LOG_INF("Disconnected: %s (reason 0x%02x)", log_strdup(addr), reason);

	err = main_publish_connection_status(addr_nole, false);
	if (err) {
		LOG_ERR("Failed to publish connection status: %d", err);
	}

	bt_conn_unref(conn);
	conninfo_free(conninfo);

	start_scan();
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(void)
{
	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
}

void main_init_bluetooth(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized");

	bt_ready();
	bt_conn_cb_register(&conn_callbacks);
	start_scan();
}

static void write_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	LOG_INF("Write complete: err 0x%02x", err);

	memset(params, 0, sizeof(*params));
}

int main_set_bluetooth_value(const bt_addr_t *addr, uint16_t handle, void *data, size_t len)
{
	struct bt_conn *conn;
	bt_addr_le_t peer = {
		.type = BT_ADDR_LE_RANDOM,
		.a = *addr,
	};
	int err;
	struct conninfo *conninfo;
	struct bt_gatt_write_params *write_params = NULL;
	size_t paramid;

	if (len == 0) {
		LOG_ERR("No data to send");
		return -EINVAL;
	}

	if (len > sizeof(conninfo->gatt_write_buf)) {
		LOG_ERR("too much data");
		return -EINVAL;
	}

	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &peer);
	if (!conn) {
		LOG_ERR("can't find connection");
		return -ENOENT;
	}

	conninfo = conninfo_find(conn);
	if (!conninfo) {
		LOG_ERR("attempted write to unknown connection");
		err = -ENOENT;
		goto unref_conn;
	}

	for (paramid = 0; paramid < ARRAY_SIZE(conninfo->write_params); paramid++) {
		if (conninfo->write_params[paramid].func == NULL) {
			write_params = &conninfo->write_params[paramid];
			break;
		}
	}
	if (!write_params) {
		LOG_ERR("No free write params");
		err = -EBUSY;
		goto unref_conn;
	}

	if (write_params->func) {
		LOG_ERR("Write ongoing");
		err = -EBUSY;
		goto unref_conn;
	}

	memcpy(conninfo->gatt_write_buf, data, len);
	write_params->length = len;
	write_params->data = conninfo->gatt_write_buf;
	write_params->handle = handle;
	write_params->offset = 0;
	write_params->func = write_func;

	err = bt_gatt_write(conn, write_params);
	if (err) {
		LOG_ERR("Write failed (err %d)", err);
		goto free_write_params;
	}

	LOG_INF("Write pending");
	bt_conn_unref(conn);
	return 0;

free_write_params:
	write_params->func = NULL;
unref_conn:
	bt_conn_unref(conn);
	return err;
}

static void publish_bond_cb(const struct bt_bond_info *info, void *ctx_)
{
	int err;
	struct bt_conn *conn;
	char addr[BT_ADDR_STR_LEN];
	bool connected;

	bt_addr_to_str(&info->addr.a, addr, sizeof(addr));

	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &info->addr);
	connected = (conn && main_bt_conn_is_connected(conn));

	if (conn) {
		bt_conn_unref(conn);
		conn = NULL;
	}

	err = main_publish_connection_status(addr, connected);
	if (err) {
		LOG_ERR("Failed to publish connection status: %d", err);
	}
}

void main_publish_all_connection_statuses(void)
{
	bt_foreach_bond(BT_ID_DEFAULT, publish_bond_cb, NULL);
}
