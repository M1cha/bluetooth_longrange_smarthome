#include "main.h"

#include <bluetooth/bluetooth.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <fatal.h>
#include <modbus/modbus.h>
#include <sys/byteorder.h>
#include <sys/printk.h>
#include <zephyr.h>

#ifdef CONFIG_USB_DEVICE_STACK
#include <usb/usb_device.h>
#endif

#include <logging/log.h>
#include <logging/log_ctrl.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct gpio_callback button_cb_data;

uint16_t g_main_meterstatus;
uint16_t g_main_alarmstatus;
uint16_t g_main_outputstatus;
uint16_t g_main_spaceco2;

static int client_iface;
static const struct modbus_iface_param client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = 50000,
	.serial = {
		.baud = 9600,
		.parity = UART_CFG_PARITY_NONE,
	},
};

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int err;

	LOG_INF("Button pressed");

	err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
	if (err) {
		LOG_ERR("bt_unpair: %d", err);
	}
}

static void init_button(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d",
			ret,
			button.port->name,
			button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
			ret,
			button.port->name,
			button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	LOG_INF("Set up button at %s pin %d", button.port->name, button.pin);
}

static int init_modbus_client(void)
{
	const char iface_name[] = { DT_PROP(DT_INST(0, zephyr_modbus_serial), label) };

	client_iface = modbus_iface_get_by_name(iface_name);

	return modbus_init_client(client_iface, client_param);
}

void main(void)
{
	int err;
	uint8_t node = 0xFE;
	uint16_t regs[4];

#ifdef CONFIG_USB_DEVICE_STACK
	err = usb_enable(NULL);
	if (err) {
		LOG_ERR("Failed to enable USB");
		return;
	}
	k_sleep(K_SECONDS(5));
	LOG_ERR("USB initialized");
#endif

	if (init_modbus_client()) {
		LOG_ERR("Modbus RTU client initialization failed");
		return;
	}

	main_init_bluetooth();

	if (!device_is_ready(button.port)) {
		LOG_ERR("Error: button device %s is not ready", button.port->name);
	} else {
		init_button();
	}

	for (;; k_sleep(K_SECONDS(5))) {
		err = modbus_read_input_regs(client_iface, node, 0x0000, regs, ARRAY_SIZE(regs));
		if (err) {
			LOG_ERR("can't read registers %d", err);
			continue;
		}

		uint16_t meterstatus = regs[0];
		uint16_t alarmstatus = regs[1];
		uint16_t outputstatus = regs[2];
		uint16_t spaceco2 = regs[3];

		LOG_INF("meter=0x%04x alarm=0x%04x output=0x%04x co2=%u",
			meterstatus,
			alarmstatus,
			outputstatus,
			spaceco2);

		if (g_main_meterstatus != meterstatus) {
			g_main_meterstatus = meterstatus;
			bt_co2_meterstatus_notify(meterstatus);
		}
		if (g_main_alarmstatus != alarmstatus) {
			g_main_alarmstatus = alarmstatus;
			bt_co2_alarmstatus_notify(alarmstatus);
		}
		if (g_main_outputstatus != outputstatus) {
			g_main_outputstatus = outputstatus;
			bt_co2_outputstatus_notify(outputstatus);
		}
		if (g_main_spaceco2 != spaceco2) {
			g_main_spaceco2 = spaceco2;
			bt_co2_spaceco2_notify(spaceco2);
		}
	}
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Resetting system");
	sys_reboot(0);
	CODE_UNREACHABLE;
}
