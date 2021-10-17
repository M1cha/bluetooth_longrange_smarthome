#include "main.h"

#include <fatal.h>
#include <sys/reboot.h>
#include <usb/usb_device.h>
#ifdef CONFIG_SHELL
#include <shell/shell.h>
#endif

#include <logging/log.h>
#include <logging/log_ctrl.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static struct k_work_delayable autoinit_work;

static void autoinit_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	main_init_bluetooth();
	main_init_mqtt();
}

void main(void)
{
	int err;

	err = usb_enable(NULL);
	if (err) {
		LOG_ERR("Failed to enable USB");
		return;
	}
	LOG_INF("USB initialized");

	k_work_init_delayable(&autoinit_work, autoinit_work_handler);
	k_work_schedule(&autoinit_work, K_SECONDS(5));
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Resetting system");
	sys_reboot(0);
	CODE_UNREACHABLE;
}

#ifdef CONFIG_SHELL
static int cmd_main_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_work_cancel_delayable(&autoinit_work);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_main,
			       SHELL_CMD(stop, NULL, "stop autoinit", cmd_main_stop),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(main, &sub_main, "main", NULL);
#endif
