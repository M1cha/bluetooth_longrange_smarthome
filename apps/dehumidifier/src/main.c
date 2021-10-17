#include "main.h"

#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <fatal.h>
#include <sys/printk.h>
#include <sys/reboot.h>
#include <zephyr.h>

#ifdef CONFIG_USB_DEVICE_STACK
#include <usb/usb_device.h>
#endif

#include <logging/log.h>
#include <logging/log_ctrl.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define DEHUMID_NODE DT_INST(0, zephyr_dehumidifier)

static const struct gpio_dt_spec ionizer = GPIO_DT_SPEC_GET_OR(DEHUMID_NODE, ionizer_gpios, { 0 });
static const struct gpio_dt_spec fan_half =
	GPIO_DT_SPEC_GET_OR(DEHUMID_NODE, fan_half_gpios, { 0 });
static const struct gpio_dt_spec fan_full =
	GPIO_DT_SPEC_GET_OR(DEHUMID_NODE, fan_full_gpios, { 0 });
static const struct gpio_dt_spec compressor =
	GPIO_DT_SPEC_GET_OR(DEHUMID_NODE, compressor_gpios, { 0 });
static const struct gpio_dt_spec waterbox =
	GPIO_DT_SPEC_GET_OR(DEHUMID_NODE, waterbox_gpios, { 0 });

static bool ionizer_active = false;
static enum fanmode fanmode = FANMODE_OFF;
static bool compressor_active = false;
static struct gpio_callback waterbox_cb_data;

static K_KERNEL_STACK_DEFINE(main_work_q_stack, CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);
struct k_work_q k_main_work_q;

static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get(spec->port, spec->pin);
}

static inline int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	return gpio_pin_set(spec->port, spec->pin, value);
}

static int gpio_pin_get_dt_safe(const struct gpio_dt_spec *spec)
{
	int ret;

	ret = gpio_pin_get_dt(spec);
	if (ret < 0) {
		printk("failed to get gpio: %d\n", ret);
		k_oops();
		return -1;
	}

	return ret;
}

static void gpio_pin_set_dt_safe(const struct gpio_dt_spec *spec, int value)
{
	int ret;

	ret = gpio_pin_set_dt(spec, value);
	if (ret) {
		printk("failed to set gpio: %d\n", ret);
		k_oops();
		return;
	}
}

int main_ionizer_get(bool *pval)
{
	*pval = ionizer_active;
	return 0;
}

int main_ionizer_set(bool val)
{
	int ret;
	bool waterbox_active;

	printk("set ionizer=%u\n", val);

	ret = main_waterbox_get(&waterbox_active);
	if (ret) {
		printk("can't get waterbox state: %d\n", ret);
		return ret;
	}

	// this probably isn't important but let's be pedantic here
	if (val && waterbox_active) {
		printk("can't enable fan when waterbox is full");
		return -ENOTSUP;
	}

	// we don't want ions to accumulate, so turn on the fan first
	if (val && fanmode == FANMODE_OFF) {
		ret = main_fan_set(FANMODE_HALF);
		if (ret) {
			printk("can't set fan to half: %d\n", ret);
			return ret;
		}
	}

	gpio_pin_set_dt_safe(&ionizer, val);
	ionizer_active = val;

	ret = bt_dehumid_ionizer_notify(val);
	if (ret) {
		printk("failed to notify ionizer change: %d", ret);
	}

	return 0;
}

int main_fan_get(enum fanmode *pmode)
{
	*pmode = fanmode;
	return 0;
}

int main_fan_set(enum fanmode mode)
{
	int ret;
	bool waterbox_active;

	printk("set fan=%u\n", mode);

	ret = main_waterbox_get(&waterbox_active);
	if (ret) {
		printk("can't get waterbox state: %d\n", ret);
		return ret;
	}

	// even with the compressor off, the waterlevel might increase depending
	// on the radiator temperature.
	if (mode != FANMODE_OFF && waterbox_active) {
		printk("can't enable fan when waterbox is full");
		return -ENOTSUP;
	}

	// implicitly turmn them off for safety reasons
	main_compressor_set(false);
	main_ionizer_set(false);

	switch (mode) {
	case FANMODE_OFF:
		gpio_pin_set_dt_safe(&fan_half, 0);
		gpio_pin_set_dt_safe(&fan_full, 0);
		break;

	case FANMODE_HALF:
		gpio_pin_set_dt_safe(&fan_half, 1);
		gpio_pin_set_dt_safe(&fan_full, 0);
		break;

	case FANMODE_FULL:
		gpio_pin_set_dt_safe(&fan_half, 0);
		gpio_pin_set_dt_safe(&fan_full, 1);
		break;

	default:
		return -ENOTSUP;
	}

	if (fanmode != mode) {
		ret = bt_dehumid_fan_notify(mode);
		if (ret) {
			printk("failed to notify fan change: %d", ret);
		}
	}

	fanmode = mode;

	return 0;
}

int main_compressor_get(bool *pval)
{
	*pval = compressor_active;
	return 0;
}

int main_compressor_set(bool val)
{
	int ret;
	bool waterbox_active;

	printk("set compressor=%u\n", val);

	ret = main_waterbox_get(&waterbox_active);
	if (ret) {
		printk("can't get waterbox state: %d\n", ret);
		return ret;
	}

	// the waterbox might overflow
	if (val && waterbox_active) {
		printk("can't enable compressor when waterbox is full");
		return -ENOTSUP;
	}

	// the radiator might get too cold
	if (val && fanmode != FANMODE_FULL) {
		ret = main_fan_set(FANMODE_FULL);
		if (ret) {
			printk("can't set fan to full: %d\n", ret);
			return ret;
		}
	}

	gpio_pin_set_dt_safe(&compressor, val);

	if (val != compressor_active) {
		ret = bt_dehumid_compressor_notify(val);
		if (ret) {
			printk("failed to notify compressor change: %d", ret);
		}
	}

	compressor_active = val;

	return 0;
}

int main_waterbox_get(bool *pval)
{
	*pval = gpio_pin_get_dt_safe(&waterbox);
	return 0;
}

static void handle_waterbox_changed(struct k_work *work)
{
	int ret;
	bool val;

	ARG_UNUSED(work);

	ret = main_waterbox_get(&val);
	if (ret) {
		return;
	}

	printk("waterbox=%u\n", val);

	if (val) {
		// implicitly turmn them off for safety reasons
		main_compressor_set(false);
		main_ionizer_set(false);
		main_fan_set(FANMODE_OFF);
	}

	ret = bt_dehumid_waterbox_notify(val);
	if (ret) {
		printk("failed to notify waterbox change: %d", ret);
	}
}
static K_WORK_DEFINE(work_waterbox_changed, handle_waterbox_changed);

static void waterbox_changed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_work_submit_to_queue(&k_main_work_q, &work_waterbox_changed);
}

static int init_gpio(const char *name, const struct gpio_dt_spec *spec, gpio_flags_t extra_flags)
{
	int ret;

	if (!device_is_ready(spec->port)) {
		printk("[%s] device %p is not ready\n", name, spec->port);
		return -1;
	}

	ret = gpio_pin_configure_dt(spec, extra_flags);
	if (ret != 0) {
		printk("[%s] failed to configure %s pin %d: %d\n",
		       name,
		       spec->port->name,
		       spec->pin,
		       ret);
		return -1;
	}

	return 0;
}

void main(void)
{
	int err;

	struct k_work_queue_config cfg = {
		.name = "mainworkq",
		.no_yield = IS_ENABLED(CONFIG_SYSTEM_WORKQUEUE_NO_YIELD),
	};
	k_work_queue_start(&k_main_work_q,
			   main_work_q_stack,
			   K_KERNEL_STACK_SIZEOF(main_work_q_stack),
			   CONFIG_SYSTEM_WORKQUEUE_PRIORITY,
			   &cfg);

#ifdef CONFIG_USB_DEVICE_STACK
	err = usb_enable(NULL);
	if (err) {
		LOG_ERR("Failed to enable USB");
		return;
	}
	k_sleep(K_SECONDS(5));
	LOG_ERR("USB initialized");
#endif

	err = init_gpio("ionizer", &ionizer, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return;
	}

	err = init_gpio("fan-half", &fan_half, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return;
	}

	err = init_gpio("fan-full", &fan_full, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return;
	}

	err = init_gpio("compressor", &compressor, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return;
	}

	err = init_gpio("waterbox", &waterbox, GPIO_INPUT);
	if (err) {
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&waterbox, GPIO_INT_EDGE_BOTH);
	if (err) {
		printk("failed to configure waterbox interrupt: %d\n", err);
		return;
	}

	gpio_init_callback(&waterbox_cb_data, waterbox_changed, BIT(waterbox.pin));

	err = gpio_add_callback(waterbox.port, &waterbox_cb_data);
	if (err) {
		printk("failed to add waterbox callbac: %d\n", err);
		return;
	}

	main_init_bluetooth();
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Resetting system");
	sys_reboot(0);
	CODE_UNREACHABLE;
}
