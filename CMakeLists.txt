cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(w6300_driver_test)

# Add missing Pico SDK include paths
zephyr_include_directories(
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_gpio/include
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_base/include
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/common/pico_base_headers/include
)

# Include main source and driver source
target_sources(app PRIVATE 
    src/main.c
    drivers/ethernet/eth_w6300.c
)

# Add driver directory to include path
target_include_directories(app PRIVATE drivers/ethernet)