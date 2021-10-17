# Bluetooth Long Range smart home

This allows building and using devices on top of [Bluetooth Long Range](https://blog.nordicsemi.com/getconnected/tested-by-nordic-bluetooth-long-range).

The core of this is the app `central` which can be flashed on a nrf52840 USB
dongle.
The dongle provides two interfaces through USB:
- USB-UART(ttyACM) with a zephyr shell
- USB ethernet with a MQTT client

The dongle acts as a MQTT client so you have to start a broker on your computer.

The dongle will automatically connect to all bonded devices. You can create
bondings using the `bt connect ...` and `bt security 2` on the USB shell.

## Additional shell commands
- `main stop`: Cancel starting main app within 5s. Required for bonding devices using `bt`.

## MQTT topics
All communication is done using hex strings. The dongle converts those from/to
binary.

The dongle also subscribes to all subscribable characteristics.

Placeholders:
- `MAC`: (random, not public) bluetooth MAC address.
- `HANDLE`: 16bit GATT database handle. must always be 4 bytes.

Supported topics:
- `bluetooth/MAC/HANDLE/set`: write to this to change the characteristic value
- `bluetooth/MAC/HANDLE/state`: subscribe to this to receive characteristic notifications
- `bluetooth/MAC/connected`: subscribe to this to receive connected/disconnected events.
   `00`: disconnected, `01`: connected.

## Home-Assistant config samples
### CO2-sensor
```yaml
sensor:
- platform: mqtt
  unique_id: "00:11:22:33:44:55"
  state_topic: "bluetooth/00:11:22:33:44:55/001b/state"
  unit_of_measurement: "ppm"
  value_template: "{{ (value[2:4] | int('', 16) * 256) + (value[0:2] | int('', 16)) }}"
  device_class: "carbon_dioxide"
  availability:
  - payload_available: "01"
    payload_not_available: "00"
    topic: "bluetooth/00:11:22:33:44:55/connected"
```

## Apps included in this repository
- `central`: The USB dongle firmware
- `co2sensor`: A [S8 LP CO2 sensor](https://senseair.com/products/size-counts/s8-lp/)
   connected to a nrf52840-dongle
- `dehumidifier`: I replaced the MCU of a Comfee DG-30 dehumidifier with a nrf52840-mdk.
  This firmware can control the relays and read the waterbox status.
