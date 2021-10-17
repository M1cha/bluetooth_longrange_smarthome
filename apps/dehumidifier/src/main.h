#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>

enum fanmode {
	FANMODE_OFF = 0x00,
	FANMODE_HALF = 0x01,
	FANMODE_FULL = 0x02,
};

void main_init_bluetooth(void);
int bt_dehumid_ionizer_notify(bool val);
int bt_dehumid_fan_notify(enum fanmode mode);
int bt_dehumid_compressor_notify(bool val);
int bt_dehumid_waterbox_notify(bool val);

int main_ionizer_get(bool *pval);
int main_ionizer_set(bool val);

int main_fan_get(enum fanmode *pmode);
int main_fan_set(enum fanmode mode);

int main_compressor_get(bool *pval);
int main_compressor_set(bool val);

int main_waterbox_get(bool *pval);

#endif /* MAIN_H */
