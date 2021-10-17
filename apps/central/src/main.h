#ifndef MAIN_H
#define MAIN_H

#include <bluetooth/addr.h>
#include <bluetooth/conn.h>

void main_init_bluetooth(void);
void main_init_mqtt(void);

int main_publish_characteristic_value(const char *addr,
				      uint16_t handle,
				      const void *data,
				      size_t data_len);
int main_publish_connection_status(const char *addr, bool connected);

bool main_bt_conn_is_connected(struct bt_conn *conn);
int main_set_bluetooth_value(const bt_addr_t *addr, uint16_t handle, void *data, size_t len);
void main_publish_all_connection_statuses(void);

#endif /* MAIN_H */
