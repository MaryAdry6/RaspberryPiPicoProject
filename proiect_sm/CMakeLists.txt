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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(proiect_sm C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1





#-------------------------------------------------------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_assets.h")
set(WEB_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../interfata_web")

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/web_converter.py" "${GENERATED_HEADER}"
    DEPENDS 
        ${CMAKE_CURRENT_SOURCE_DIR}/web_converter.py
        ${WEB_INTERFACE_DIR}/index.html
        ${WEB_INTERFACE_DIR}/control.html
        ${WEB_INTERFACE_DIR}/delay.html
        ${WEB_INTERFACE_DIR}/distors.html
        ${WEB_INTERFACE_DIR}/reverb.html
        ${WEB_INTERFACE_DIR}/js/script.js
        ${WEB_INTERFACE_DIR}/css/stil.css
    COMMENT "Building binary payload structures from ../interfata_web..."
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(proiect_sm 
    proiect_sm.c 
    ${GENERATED_HEADER}
)
#-------------------------------------------------------------------------------------------------------------------------









pico_set_program_name(proiect_sm "proiect_sm")
pico_set_program_version(proiect_sm "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(proiect_sm 0)
pico_enable_stdio_usb(proiect_sm 1)

# Add the standard library to the build
target_link_libraries(proiect_sm
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(proiect_sm PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(proiect_sm 
        hardware_i2c
        hardware_dma
        hardware_irq
        hardware_adc
        hardware_pwm
        hardware_timer
        hardware_clocks
        pico_cyw43_arch_lwip_poll
        pico_stdlib
        )

pico_add_extra_outputs(proiect_sm)

