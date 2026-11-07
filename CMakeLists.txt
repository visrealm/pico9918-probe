cmake_minimum_required(VERSION 3.12)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

option(PICO9918_HOST_BUS "Pico 2 PICO9918 bus fixture (CDC commands instead of UART)" OFF)
if(PICO9918_HOST_BUS)
    set(DEBUG_ON_PICO ON CACHE BOOL "Build on Pico" FORCE)
    set(PICO_BOARD pico2 CACHE STRING "Fixture board" FORCE)
endif()

include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
include(FreeRTOS_Kernel_import.cmake)

project(debugprobe)

pico_sdk_init()

if (${PICO_SDK_VERSION_MAJOR} LESS 2)
	message(SEND_ERROR "Version 2 of the Pico SDK is required to compile this project. Please update your installation at ${PICO_SDK_PATH}")
endif ()

add_executable(debugprobe
        src/probe_config.c
        src/main.c
        src/usb_descriptors.c
        src/probe.c
        src/get_serial.c
        src/sw_dp_pio.c
        src/tusb_edpt_handler.c
)
if(PICO9918_HOST_BUS)
    target_sources(debugprobe PRIVATE src/pico9918_bus.c src/pico9918_protocol.c src/pico9918_cdc.c src/pico9918_boot.c)
    target_compile_definitions(debugprobe PRIVATE PICO9918_HOST_BUS=1)
    pico_generate_pio_header(debugprobe ${CMAKE_CURRENT_LIST_DIR}/src/pico9918_bus.pio)
else()
    target_sources(debugprobe PRIVATE src/cdc_uart.c src/autobaud.c)
endif()

target_sources(debugprobe PRIVATE
        CMSIS_DAP/CMSIS/DAP/Firmware/Source/DAP.c
        CMSIS_DAP/CMSIS/DAP/Firmware/Source/JTAG_DP.c
        CMSIS_DAP/CMSIS/DAP/Firmware/Source/DAP_vendor.c
        CMSIS_DAP/CMSIS/DAP/Firmware/Source/SWO.c
        #CMSIS_DAP/CMSIS/DAP/Firmware/Source/SW_DP.c
        )

target_include_directories(debugprobe PRIVATE
        CMSIS_DAP/CMSIS/DAP/Firmware/Include/
        CMSIS_DAP/CMSIS/Core/Include/
        include/
        )

target_compile_options(debugprobe PRIVATE -Wall)

pico_generate_pio_header(debugprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)
pico_generate_pio_header(debugprobe ${CMAKE_CURRENT_LIST_DIR}/src/probe_oen.pio)
pico_generate_pio_header(debugprobe ${CMAKE_CURRENT_LIST_DIR}/src/autobaud.pio)

target_include_directories(debugprobe PRIVATE src)

# add version
add_custom_target(version
        ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(debugprobe PRIVATE
        ${CMAKE_BINARY_DIR}/generated
        )

add_dependencies(debugprobe version)

target_compile_definitions (debugprobe PRIVATE
	PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
)

option (DEBUG_ON_PICO "Compile firmware for the Pico instead of Debug Probe" OFF)
if (DEBUG_ON_PICO)
    target_compile_definitions (debugprobe PRIVATE
	DEBUG_ON_PICO=1
    )
    if (PICO_BOARD STREQUAL "pico")
        set_target_properties(debugprobe PROPERTIES
            OUTPUT_NAME "debugprobe_on_pico"
        )
    elseif (PICO_BOARD STREQUAL "pico2")
        set_target_properties(debugprobe PROPERTIES
            OUTPUT_NAME "debugprobe_on_pico2"
        )
    else ()
        message(SEND_ERROR "Unsupported board ${PICO_BOARD}")
    endif ()
endif ()

if(PICO9918_HOST_BUS)
    if(NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
        message(FATAL_ERROR "PICO9918 fixture requires Pico 2 / rp2350-arm-s")
    endif()
    set_target_properties(debugprobe PROPERTIES OUTPUT_NAME "debugprobe_pico9918")
endif()


target_link_libraries(debugprobe PRIVATE
        pico_multicore
        pico_stdlib
        pico_usb_reset
        pico_unique_id
        tinyusb_device
        tinyusb_board
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_clocks
        hardware_adc
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap1
)

pico_set_binary_type(debugprobe copy_to_ram)

pico_add_extra_outputs(debugprobe)
