#include <bluetooth/addr.h>
#include <net/mqtt.h>
#include <net/net_context.h>
#include <net/net_core.h>
#include <net/net_if.h>
#include <net/net_mgmt.h>
#include <net/socket.h>
#include <random/rand32.h>
#include <stdio.h>
#include <sys/util.h>

#include "main.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main_mqtt, LOG_LEVEL_DBG);

#define APP_RECV_TIMEOUT_MS 2000
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500
#define APP_MQTT_BUFFER_SIZE 128
#define MQTT_CLIENTID "zephyr_publisher"

static struct net_mgmt_event_callback mgmt_cb;
static K_THREAD_STACK_DEFINE(mqtt_stack_area, 4096);
static struct k_thread mqtt_thread_data;
static char topic_buf[PATH_MAX];
static char publish_data_buf[APP_MQTT_BUFFER_SIZE];
static struct mqtt_data {
	/* Buffers for MQTT client. */
	uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
	uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

	/* The mqtt client struct */
	struct mqtt_client client_ctx;

	/* MQTT Broker details. */
	struct sockaddr_storage broker;

	struct zsock_pollfd fds[1];
	int nfds;

	bool connected;
} mqtt_data;

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		mqtt_data.fds[0].fd = client->transport.tcp.sock;
	} else {
		LOG_WRN("unsupported mqtt transport type: %d", client->transport.type);
	}

	mqtt_data.fds[0].events = ZSOCK_POLLIN;
	mqtt_data.nfds = 1;
}

static void clear_fds(void)
{
	mqtt_data.nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (mqtt_data.nfds > 0) {
		ret = zsock_poll(mqtt_data.fds, mqtt_data.nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

static int read_payload(void *data_, size_t len)
{
	int ret;
	uint8_t *data = data_;

	while (len) {
		ret = mqtt_read_publish_payload(&mqtt_data.client_ctx, data, MIN(len, INT_MAX));
		if (ret == -EAGAIN) {
			ret = wait(APP_RECV_TIMEOUT_MS);
			if (ret == 0) {
				LOG_ERR("publish payload receive timeout");
				return -ETIMEDOUT;
			}
			continue;
		} else if (ret < 0) {
			LOG_ERR("failed to read payload: %d", ret);
			return ret;
		} else if (ret > len) {
			LOG_ERR("read more than requested");
			return -EIO;
		}

		data += ret;
		len -= ret;
	}

	return 0;
}

static int get_path_segment(const struct mqtt_utf8 *path, size_t n, struct mqtt_utf8 *out)
{
	const uint8_t *utf8 = path->utf8;
	uint32_t size = path->size;

	while (n) {
		if (size == 0) {
			return -ENOENT;
		}
		if (*utf8 == '/') {
			n--;
		}

		utf8++;
		size--;
	}

	out->utf8 = utf8;

	for (out->size = 0; utf8[out->size] != '/'; out->size++) {
	}

	return 0;
}

static void handle_publish(int result, const struct mqtt_publish_param *param)
{
	const struct mqtt_publish_message *message = &param->message;
	struct mqtt_puback_param puback;
	struct mqtt_pubrec_param pubrec;
	static char data[APP_MQTT_BUFFER_SIZE];
	static char rawdata[APP_MQTT_BUFFER_SIZE];
	int ret;
	struct mqtt_utf8 mac;
	struct mqtt_utf8 handle;
	char mac0[BT_ADDR_STR_LEN];
	bt_addr_t btaddr;
	char handle0[5];
	unsigned long handle_ul;
	size_t binlen;

	LOG_INF("MQTT publish received %d, %u bytes", result, message->payload.len);
	LOG_INF(" id: %d, qos: %d", param->message_id, message->topic.qos);
	LOG_HEXDUMP_DBG(message->topic.topic.utf8, message->topic.topic.size, "topic");

	if (message->payload.len > sizeof(data)) {
		uint32_t len = message->payload.len;

		LOG_WRN("message payload is too big, discard");

		while (len) {
			size_t toread = MIN(len, sizeof(data));

			ret = read_payload(data, toread);
			if (ret) {
				LOG_ERR("can't read payload: %d", ret);
				return;
			}

			len -= toread;
		}

		goto ack;
	}

	ret = read_payload(data, message->payload.len);
	if (ret) {
		LOG_ERR("can't read payload: %d", ret);
		return;
	}

	LOG_HEXDUMP_DBG(data, message->payload.len, "payload");

	binlen = hex2bin(data, message->payload.len, rawdata, ARRAY_SIZE(rawdata));
	if (!binlen) {
		LOG_ERR("can't convert payload from hex: %d", ret);
		return;
	}

	ret = get_path_segment(&message->topic.topic, 1, &mac);
	if (ret) {
		LOG_ERR("can't get mac from topic");
		goto ack;
	}

	ret = get_path_segment(&message->topic.topic, 2, &handle);
	if (ret) {
		LOG_ERR("can't get handle from topic");
		goto ack;
	}

	LOG_HEXDUMP_DBG(mac.utf8, mac.size, "mac");
	LOG_HEXDUMP_DBG(handle.utf8, handle.size, "handle");

	if (mac.size != sizeof(mac0) - 1) {
		LOG_ERR("invalid mac length");
		goto ack;
	}
	memcpy(mac0, mac.utf8, mac.size);
	mac0[mac.size] = 0;

	if (handle.size != sizeof(handle0) - 1) {
		LOG_ERR("invalid handle length");
		goto ack;
	}
	memcpy(handle0, handle.utf8, handle.size);
	handle0[handle.size] = 0;

	ret = bt_addr_from_str(mac0, &btaddr);
	if (ret) {
		LOG_ERR("can't parse bluetooth addr");
		goto ack;
	}

	errno = 0;
	handle_ul = strtoul(handle0, NULL, 16);
	if (handle_ul == 0 || handle_ul == ULONG_MAX || errno) {
		LOG_ERR("can't parse handle: %d", errno);
		goto ack;
	}

	ret = main_set_bluetooth_value(&btaddr, handle_ul, rawdata, binlen);
	if (ret) {
		LOG_ERR("can't set value: %d", ret);
		goto ack;
	}

ack:
	switch (message->topic.qos) {
	case MQTT_QOS_0_AT_MOST_ONCE:
		break;

	case MQTT_QOS_1_AT_LEAST_ONCE:
		puback.message_id = param->message_id;

		ret = mqtt_publish_qos1_ack(&mqtt_data.client_ctx, &puback);
		if (ret) {
			LOG_ERR("Failed to send MQTT PUBACK: %d", ret);
		}
		break;

	case MQTT_QOS_2_EXACTLY_ONCE:
		pubrec.message_id = param->message_id;

		ret = mqtt_publish_qos2_receive(&mqtt_data.client_ctx, &pubrec);
		if (ret) {
			LOG_ERR("Failed to send MQTT PUBREC: %d", ret);
		}
		break;

	default:
		LOG_ERR("unsupported QOS for PUBLISH: %u", message->topic.qos);
		break;
	}
}

static void mqtt_evt_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		mqtt_data.connected = true;
		LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);

		mqtt_data.connected = false;
		clear_fds();

		break;

	case MQTT_EVT_PUBLISH:
		handle_publish(evt->result, &evt->param.publish);
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = { .message_id =
								     evt->param.pubrec.message_id };

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBREL:
		if (evt->result) {
			LOG_ERR("MQTT PUBREL error %d", evt->result);
			break;
		}

		LOG_INF("PUBREL packet id: %u", evt->param.pubrel.message_id);

		const struct mqtt_pubcomp_param comp_param = {
			.message_id = evt->param.pubrel.message_id
		};

		err = mqtt_publish_qos2_complete(client, &comp_param);
		if (err) {
			LOG_ERR("Failed to send MQTT PUBCOMP: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u", evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_SUBACK:
		LOG_INF("SUBACK packet");
		break;

	case MQTT_EVT_UNSUBACK:
		LOG_INF("UNSUBACK packet");
		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		LOG_WRN("unsupported MQTT event: %d", evt->type);
		break;
	}
}

static int init_broker(void)
{
	struct net_if *iface;
	struct net_if_ipv4 *ipv4;
	char buf[NET_IPV4_ADDR_LEN];
	const char *gw;
	int rc;
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&mqtt_data.broker;

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("no default netif available");
		return -1;
	}

	ipv4 = iface->config.ip.ipv4;
	if (!ipv4) {
		LOG_ERR("no ipv4 address");
		return -1;
	}

	gw = net_addr_ntop(AF_INET, &ipv4->gw, buf, sizeof(buf));
	if (!gw) {
		LOG_ERR("can't convert ipv4 gateway to string");
		return -1;
	}

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(1883);

	rc = zsock_inet_pton(AF_INET, gw, &broker4->sin_addr);
	if (rc != 1) {
		LOG_ERR("can't convert ipv4 gateway to zsock format");
		return -1;
	}

	return 0;
}

static void subscribe(void)
{
	int err;
	const char *topic = "bluetooth/+/+/set";
	struct mqtt_topic subs_topic = { .topic = { .utf8 = topic, .size = strlen(topic) },
					 .qos = MQTT_QOS_2_EXACTLY_ONCE };
	const struct mqtt_subscription_list subs_list = { .list = &subs_topic,
							  .list_count = 1U,
							  .message_id = 1U };

	err = mqtt_subscribe(&mqtt_data.client_ctx, &subs_list);
	if (err) {
		LOG_ERR("Failed to subscribe to %s item, error %d", subs_topic.topic.utf8, err);
		return;
	}

	LOG_INF("subscription requested");
}

static void connect_and_wait(void)
{
	int rc;
	struct mqtt_client *client = &mqtt_data.client_ctx;

	while (!mqtt_data.connected) {
		rc = init_broker();
		if (rc) {
			LOG_ERR("failed to init broker: %d", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		rc = mqtt_connect(client);
		if (rc != 0) {
			LOG_ERR("mqtt_connect failed: %d", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			mqtt_input(client);
		}

		if (!mqtt_data.connected) {
			mqtt_abort(client);
		}
	}

	LOG_INF("MQTT is now connected");
	subscribe();
	main_publish_all_connection_statuses();
}

static int mqtt_process_connection(void)
{
	struct mqtt_client *client = &mqtt_data.client_ctx;
	int rc;

	while (mqtt_data.connected) {
		if (wait(mqtt_keepalive_time_left(client))) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input failed: %d", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live failed: %d", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input failed: %d", rc);
				return rc;
			}
		}
	}

	return 0;
}

static void mqtt_thread(void *_a, void *_b, void *_c)
{
	int rc;
	struct mqtt_client *client = &mqtt_data.client_ctx;

	LOG_INF("MQTT thread started");

	for (;;) {
		connect_and_wait();

		rc = mqtt_process_connection();
		LOG_INF("mqtt_process_connection returned with: %d", rc);

		if (mqtt_data.connected) {
			mqtt_disconnect(client);
		} else {
			mqtt_abort(client);
		}
	}
}

static void
net_mgmt_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].addr_type != NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("Your address: %s",
			log_strdup(net_addr_ntop(AF_INET,
						 &iface->config.ip.ipv4->unicast[i].address.in_addr,
						 buf,
						 sizeof(buf))));
		LOG_INF("Lease time: %u seconds", iface->config.dhcpv4.lease_time);
		LOG_INF("Subnet: %s",
			log_strdup(net_addr_ntop(
				AF_INET, &iface->config.ip.ipv4->netmask, buf, sizeof(buf))));
		LOG_INF("Router: %s",
			log_strdup(net_addr_ntop(
				AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf))));
	}
}

void main_init_mqtt(void)
{
	struct mqtt_client *client = &mqtt_data.client_ctx;

	mqtt_client_init(client);

	/* MQTT client configuration */
	client->broker = &mqtt_data.broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = mqtt_data.rx_buffer;
	client->rx_buf_size = sizeof(mqtt_data.rx_buffer);
	client->tx_buf = mqtt_data.tx_buffer;
	client->tx_buf_size = sizeof(mqtt_data.tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;

	net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	net_dhcpv4_start(net_if_get_default());

	k_thread_create(&mqtt_thread_data,
			mqtt_stack_area,
			K_THREAD_STACK_SIZEOF(mqtt_stack_area),
			mqtt_thread,
			NULL,
			NULL,
			NULL,
			K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1),
			0,
			K_NO_WAIT);
}

int main_publish_characteristic_value(const char *addr,
				      uint16_t handle,
				      const void *data,
				      size_t data_len)
{
	struct mqtt_publish_param param;
	int rc;
	size_t hex_len;

	if (!mqtt_data.connected) {
		return -ENOTCONN;
	}

	rc = snprintf(topic_buf, sizeof(topic_buf), "bluetooth/%s/%04x/state", addr, handle);
	if (rc < 0 || (size_t)rc >= sizeof(topic_buf)) {
		return -ENOMEM;
	}

	hex_len = bin2hex(data, data_len, publish_data_buf, sizeof(publish_data_buf));
	if (!hex_len) {
		return -ENOMEM;
	}

	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = (uint8_t *)topic_buf;
	param.message.topic.topic.size = (size_t)rc;
	param.message.payload.data = publish_data_buf;
	param.message.payload.len = hex_len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 1U;

	return mqtt_publish(&mqtt_data.client_ctx, &param);
}

int main_publish_connection_status(const char *addr, bool connected)
{
	struct mqtt_publish_param param;
	int rc;

	if (!mqtt_data.connected) {
		return -ENOTCONN;
	}

	rc = snprintf(topic_buf, sizeof(topic_buf), "bluetooth/%s/connected", addr);
	if (rc < 0 || (size_t)rc >= sizeof(topic_buf)) {
		return -ENOMEM;
	}

	if (sizeof(topic_buf) < 2) {
		return -ENOMEM;
	}

	publish_data_buf[0] = '0';
	publish_data_buf[1] = connected ? '1' : '0';

	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = (uint8_t *)topic_buf;
	param.message.topic.topic.size = (size_t)rc;
	param.message.payload.data = publish_data_buf;
	param.message.payload.len = 2;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 1U;

	return mqtt_publish(&mqtt_data.client_ctx, &param);
}
