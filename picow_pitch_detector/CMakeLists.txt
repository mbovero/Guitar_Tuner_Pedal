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

project(picow_pitch_detector C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(picow_pitch_detector 
    src/main.cpp 
    src/guitar_input.cpp
    src/crossing_history.cpp
    src/note_tracker.cpp
)

target_compile_definitions(picow_pitch_detector PRIVATE
    TUNER_DEBUG=1
)

pico_set_program_name(picow_pitch_detector "picow_pitch_detector")
pico_set_program_version(picow_pitch_detector "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(picow_pitch_detector 0)
pico_enable_stdio_usb(picow_pitch_detector 1)

# Add the standard library to the build
target_link_libraries(picow_pitch_detector
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(picow_pitch_detector PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

# Add any user requested libraries
target_link_libraries(picow_pitch_detector 
        hardware_spi
        hardware_dma
        hardware_adc
        )

pico_add_extra_outputs(picow_pitch_detector)

