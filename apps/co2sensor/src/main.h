#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

extern uint16_t g_main_meterstatus;
extern uint16_t g_main_alarmstatus;
extern uint16_t g_main_outputstatus;
extern uint16_t g_main_spaceco2;

void main_init_bluetooth(void);
int bt_co2_meterstatus_notify(uint16_t val);
int bt_co2_alarmstatus_notify(uint16_t val);
int bt_co2_outputstatus_notify(uint16_t val);
int bt_co2_spaceco2_notify(uint16_t val);

#endif /* MAIN_H */
