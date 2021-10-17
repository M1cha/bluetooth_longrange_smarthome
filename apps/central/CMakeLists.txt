cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(central)

zephyr_library_named(main_bluetooth_internal)
zephyr_library_sources(
    src/bluetooth_internal.c
)
target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PRIVATE
    ${ZEPHYR_BASE}/subsys/bluetooth/host
)

target_sources(app PRIVATE
    src/bluetooth.c
    src/main.c
    src/mqtt.c
)
target_link_libraries(app PRIVATE
    main_bluetooth_internal
)
