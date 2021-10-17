#include <bluetooth/buf.h>
#include <bluetooth/conn.h>

#include "conn_internal.h"

bool main_bt_conn_is_connected(struct bt_conn *conn)
{
	return (conn->state == BT_CONN_CONNECTED);
}
