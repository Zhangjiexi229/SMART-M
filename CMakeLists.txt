cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name (auto-detect from folder name)
get_filename_component(CMAKE_PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# usrcode 各层源文件自动收集
file(GLOB COMMON_SOURCES   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/usrcode/Common/Src/*.c")
file(GLOB PROTOCOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/usrcode/Protocol/Src/*.c")
file(GLOB BSP_SOURCES      CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/usrcode/BSP/Src/*.c")
file(GLOB APP_SOURCES      CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/usrcode/APP/Src/*.c")

# Paho MQTT Embedded C —— 仅编译客户端需要的文件（排除*Server.c服务端和MQTTFormat工具）
set(PAHO_MQTT_PACKET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTPacket.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTConnectClient.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTDeserializePublish.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTSerializePublish.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTSubscribeClient.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src/MQTTUnsubscribeClient.c"
)
set(PAHO_MQTT_CLIENT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTClient-C/src/MQTTClient.c"
)

# FreeModbus 主从一体（RTU模式）
# FreeModbus 主从一体(RTU模式) - F407VET6新板适配: APP_MODBUS_ENABLE=0 暂停编译
# (PB1 被板载 LCD 背光上拉 R13 15K 占用; 毕设主线为 MQTT 云)
set(FREEMODBUS_SOURCES
    "")


# Paho MQTTClient.h 通过 MQTTCLIENT_PLATFORM_HEADER 宏自动包含平台头文件，
# 以获取 Timer 和 Network 类型定义。仅给 paho 源文件添加此宏，
# 避免影响工程其他文件。mqtt_port.h 在 Protocol/Inc 中，已在 include path 里。
set_source_files_properties(
    ${PAHO_MQTT_PACKET_SOURCES}
    ${PAHO_MQTT_CLIENT_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "MQTTCLIENT_PLATFORM_HEADER=mqtt_port.h"
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here

    ${COMMON_SOURCES}
    ${PROTOCOL_SOURCES}
    ${BSP_SOURCES}
    ${APP_SOURCES}
    ${PAHO_MQTT_PACKET_SOURCES}
    ${PAHO_MQTT_CLIENT_SOURCES}
    ${FREEMODBUS_SOURCES}

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths

    ${CMAKE_CURRENT_SOURCE_DIR}/usrcode/Common/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/usrcode/Protocol/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/usrcode/BSP/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/usrcode/APP/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTPacket/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/paho/MQTTClient-C/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/freemodbus/modbus/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/freemodbus/modbus/rtu
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/freemodbus/port
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    m

    # Add user defined libraries
)

# Generate .hex and .bin after linking
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating HEX/BIN and printing size"
)