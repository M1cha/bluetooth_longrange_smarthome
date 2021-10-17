cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(co2sensor)

target_sources(app PRIVATE
    src/bluetooth.c
    src/bt_service_co2.c
    src/main.c
)
