# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picow_tuner_firmware C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(picow_tuner_firmware 
    src/main.cpp 
    src/adc_input_handler.cpp
    src/tuning_feedback.cpp
)

target_compile_definitions(picow_tuner_firmware PRIVATE
    TUNER_DEBUG=1
)

pico_set_program_name(picow_tuner_firmware "picow_tuner_firmware")
pico_set_program_version(picow_tuner_firmware "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(picow_tuner_firmware 0)
pico_enable_stdio_usb(picow_tuner_firmware 1)

# Add the standard library to the build
target_link_libraries(picow_tuner_firmware
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(picow_tuner_firmware PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

# Add any user requested libraries
target_link_libraries(picow_tuner_firmware 
        hardware_spi
        hardware_dma
        hardware_adc
        )

pico_add_extra_outputs(picow_tuner_firmware)

