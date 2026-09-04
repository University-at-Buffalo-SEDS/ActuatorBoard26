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

# Set the project name
set(CMAKE_PROJECT_NAME ActuationBoard)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)
option(SEDS_FIRMWARE_SIM_TEST "Enable simulator-only network observability" OFF)
if(SEDS_FIRMWARE_SIM_TEST)
    add_compile_definitions(SEDS_FIRMWARE_SIM_TEST=1)
endif()

include(FetchContent)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# Keep third-party sources out of the repository while pinning known-good
# revisions for reproducible firmware builds. FetchContent remains connected so
# a newly pinned revision is pulled automatically during configure.

FetchContent_Declare(
    sedslaunchcore
    GIT_REPOSITORY https://github.com/University-at-Buffalo-SEDS/SEDSLaunchCore.git
    GIT_TAG 7cdbca87b7fad2c72d73257f4fb1b14df6b280a0
    GIT_SHALLOW FALSE
)
FetchContent_GetProperties(sedslaunchcore)
if(NOT sedslaunchcore_POPULATED)
    FetchContent_Populate(sedslaunchcore)
endif()

include("${sedslaunchcore_SOURCE_DIR}/cmake/launchcore_stm32.cmake")
launchcore_generate_linker_scripts(
    BSP_CONFIG "${CMAKE_SOURCE_DIR}/Bootloader/board_config.h"
    LAYOUT_PREFIX ACTUATOR
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/launchcore-linker"
    OUT_BOOTLOADER_SCRIPT LAUNCHCORE_BOOTLOADER_LINKER
    OUT_APPLICATION_SCRIPT LAUNCHCORE_APPLICATION_LINKER)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USER_VECT_TAB_ADDRESS VECT_TAB_OFFSET=0x00004200U)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T "${LAUNCHCORE_APPLICATION_LINKER}"
)
# LaunchCore leaves only 472 KiB for the packaged application. Keep C sources
# size-optimized in Debug too (debug symbols remain available).
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Os)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
target_compile_definitions(STM32_Drivers PRIVATE
    USER_VECT_TAB_ADDRESS VECT_TAB_OFFSET=0x00004200U)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/can_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/telemetry_thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/telemetry.c 
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/telemetry_hooks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/thread_comm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/safety_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ota_stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/flight_state_cache.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/persistent_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/spark_stuff/igniter_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/sol_stuff/solenoid_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/stepper_stuff/stepper_driver.c
)

set(LAUNCHCORE_APP_RUNTIME_SOURCES
    "${sedslaunchcore_SOURCE_DIR}/bootloader/src/crc32.c"
    "${sedslaunchcore_SOURCE_DIR}/bootloader/src/metadata.c"
    "${sedslaunchcore_SOURCE_DIR}/bootloader/src/persist.c"
    "${sedslaunchcore_SOURCE_DIR}/update_lib/src/delta_update.c"
    "${sedslaunchcore_SOURCE_DIR}/update_lib/src/confirm_boot.c"
    "${sedslaunchcore_SOURCE_DIR}/update_lib/src/update_status.c"
    "${CMAKE_SOURCE_DIR}/Core/Src/launchcore_delta_format.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/storage_dispatch.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/storage_internal_flash.c")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${LAUNCHCORE_APP_RUNTIME_SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/Bootloader"
    "${sedslaunchcore_SOURCE_DIR}/bootloader/include"
    "${sedslaunchcore_SOURCE_DIR}/update_lib/include")

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/spark_stuff
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/sol_stuff
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/stepper_stuff
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

    # Add user defined libraries
)

# SEDSNet v4 runtime-schema networking. FetchContent copies the board-owned
# schema into the crate before Cargo runs because embedded/no_std builds bundle
# that file instead of opening a filesystem path at runtime.
set(SEDSNET_SCHEMA_FILE "${CMAKE_SOURCE_DIR}/config/sedsnet.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SEDSNET_SCHEMA_FILE}")
set(SEDSNET_DEVICE_IDENTIFIER "AB" CACHE STRING "" FORCE)
set(SEDSNET_MAX_STACK_PAYLOAD "128" CACHE STRING "" FORCE)
set(SEDSNET_MAX_QUEUE_BUDGET "8192" CACHE STRING
    "Bounded reliable/discovery history shared with the network peer" FORCE)
set(SEDSNET_MAX_RECENT_RX_IDS "16" CACHE STRING "" FORCE)
set(SEDSNET_ENV_STARTING_QUEUE_SIZE "512" CACHE STRING "" FORCE)
set(SEDSNET_ENV_QUEUE_GROW_STEP "1.0" CACHE STRING "" FORCE)
set(SEDSNET_ENV_PAYLOAD_COMPRESS_THRESHOLD "30" CACHE STRING "" FORCE)
set(SEDSNET_FORCE_RELEASE ON CACHE BOOL
    "Build SEDSNet in release mode even for Debug firmware builds" FORCE)
set(SEDSNET_TARGET "thumbv7em-none-eabihf" CACHE STRING "" FORCE)
set(SEDSNET_EMBEDDED_BUILD ON CACHE BOOL "" FORCE)
set(SEDSNET_ENABLE_CRYPTOGRAPHY OFF CACHE BOOL
    "Preserve the current unencrypted CAN deployment and reduce flash/RAM use" FORCE)

FetchContent_Declare(
    sedsnet
    GIT_REPOSITORY https://github.com/Rylan-Meilutis/SEDSnet.git
    GIT_TAG 2b4ded484ffafe1d33c5bae57966125923966970
    GIT_SHALLOW FALSE
    PATCH_COMMAND ${CMAKE_COMMAND}
                  -DSEDSNET_SOURCE_DIR=<SOURCE_DIR>
                  -DSEDSNET_SCHEMA_FILE=${SEDSNET_SCHEMA_FILE}
                  -DSEDSNET_CRC32_DIR=${CMAKE_SOURCE_DIR}/third_party/embedded-crc32fast
                  -P ${CMAKE_SOURCE_DIR}/cmake/prepare_sedsnet.cmake
)
FetchContent_MakeAvailable(sedsnet)
configure_file("${SEDSNET_SCHEMA_FILE}"
               "${sedsnet_SOURCE_DIR}/telemetry_config.json" COPYONLY)
file(TOUCH "${sedsnet_SOURCE_DIR}/build.rs")
target_link_libraries(${CMAKE_PROJECT_NAME} sedsnet::sedsnet)
add_dependencies(${CMAKE_PROJECT_NAME} sedsnet_build)
set(TELEMETRY_TIMESYNC_MASTER_PRIO "-1" CACHE STRING "Timesync source priority. Values < 0 make this board a consumer; values >= 0 make it a source with that priority." FORCE)

# LaunchCore bootloader. The board port is kept in this repository while the
# bootloader implementation and image tools come from the fetched upstream.
set(LAUNCHCORE_CORE_SOURCES
    app_install.c
    boot_select.c
    bootloader_update.c
    bundle.c
    delta.c
    crc32.c
    image_validate.c
    metadata.c
    persist.c
    recovery_install.c
    sha256.c
)
list(TRANSFORM LAUNCHCORE_CORE_SOURCES
     PREPEND "${sedslaunchcore_SOURCE_DIR}/bootloader/src/")

add_executable(ActuationBoardBootloader
    ${LAUNCHCORE_CORE_SOURCES}
    "${sedslaunchcore_SOURCE_DIR}/bootloader/src/main.c"
    "${sedslaunchcore_SOURCE_DIR}/bootloader/src/boot_jump.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/platform.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/recovery_transport.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/storage_dispatch.c"
    "${CMAKE_SOURCE_DIR}/Bootloader/storage_internal_flash.c"
    "${CMAKE_SOURCE_DIR}/Core/Src/system_stm32g4xx.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c"
    "${CMAKE_SOURCE_DIR}/startup_stm32g491xx.s"
)
target_include_directories(ActuationBoardBootloader PRIVATE
    "${CMAKE_SOURCE_DIR}/Bootloader"
    "${CMAKE_SOURCE_DIR}/Core/Inc"
    "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include"
    "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc"
    "${sedslaunchcore_SOURCE_DIR}/bootloader/include"
)
target_compile_definitions(ActuationBoardBootloader PRIVATE
    USE_HAL_DRIVER
    STM32G491xx
)
target_link_options(ActuationBoardBootloader PRIVATE
    -T "${LAUNCHCORE_BOOTLOADER_LINKER}"
    -Wl,-Map=ActuationBoardBootloader.map
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)
target_link_libraries(ActuationBoardBootloader m)

set(LAUNCHCORE_APP_VERSION "1.0.0" CACHE STRING
    "Version stored in the packaged LaunchCore application image")
set(ACTUATOR_APP_BIN "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin")
set(ACTUATOR_APP_IMAGE
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.launchcore.img")
set(ACTUATOR_BOOTLOADER_BIN
    "${CMAKE_CURRENT_BINARY_DIR}/ActuationBoardBootloader.bin")
set(ACTUATOR_FACTORY_IMAGE
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.factory.bin")

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            "${ACTUATOR_APP_BIN}"
    COMMAND "${Python3_EXECUTABLE}" "${sedslaunchcore_SOURCE_DIR}/tools/mkimage.py"
            --input "${ACTUATOR_APP_BIN}"
            --output "${ACTUATOR_APP_IMAGE}"
            --slot-base 0x08004000
            --vector-table 0x08004200
            --header-size 0x200
            --version "${LAUNCHCORE_APP_VERSION}"
            --xip
    VERBATIM
)
add_custom_command(TARGET ActuationBoardBootloader POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ActuationBoardBootloader>
            "${ACTUATOR_BOOTLOADER_BIN}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${ACTUATOR_FACTORY_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${sedslaunchcore_SOURCE_DIR}/tools/mkfactory.py"
            --bootloader "${ACTUATOR_BOOTLOADER_BIN}"
            --bootloader-base 0x08000000
            --app "${ACTUATOR_APP_IMAGE}"
            --app-base 0x08004000
            --metadata-base 0x0807E000
            --output "${ACTUATOR_FACTORY_IMAGE}"
    DEPENDS ${CMAKE_PROJECT_NAME} ActuationBoardBootloader
    VERBATIM
)
add_custom_target(factory-image ALL DEPENDS "${ACTUATOR_FACTORY_IMAGE}")
launchcore_add_memory_report(
    NAME launchcore-memory-report
    BOOTLOADER_TARGET ActuationBoardBootloader
    APPLICATION_TARGET ${CMAKE_PROJECT_NAME}
    BOOTLOADER_BINARY "${ACTUATOR_BOOTLOADER_BIN}"
    FIRMWARE_BINARY "${ACTUATOR_APP_BIN}"
    BOOTLOADER_CAPACITY 0x4000
    FIRMWARE_CAPACITY 0x73E00
    TOTAL_FLASH_SIZE 0x80000)
add_dependencies(factory-image launchcore-memory-report)

set(LAUNCHCORE_DELTA_BASE_IMAGE "" CACHE FILEPATH
    "Previously installed .launchcore.img used to generate a reversible OTA delta")
if(LAUNCHCORE_DELTA_BASE_IMAGE)
    set(ACTUATOR_DELTA_IMAGE
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.launchcore.delta")
    add_custom_command(
        OUTPUT "${ACTUATOR_DELTA_IMAGE}"
        COMMAND "${Python3_EXECUTABLE}" "${sedslaunchcore_SOURCE_DIR}/tools/mkdelta.py"
                --base "${LAUNCHCORE_DELTA_BASE_IMAGE}"
                --target "${ACTUATOR_APP_IMAGE}"
                --output "${ACTUATOR_DELTA_IMAGE}"
                --erase-size 0x800
                --slot-size 0x74000
                --delta-slot-size 0x6000
        DEPENDS ${CMAKE_PROJECT_NAME} "${LAUNCHCORE_DELTA_BASE_IMAGE}"
        VERBATIM)
    add_custom_target(delta-image DEPENDS "${ACTUATOR_DELTA_IMAGE}")
endif()

# Option defaults to ON
option(ENABLE_TELEMETRY "Enable the telemetry subsystem" ON)

# Print its state for debugging
message(STATUS "Telemetry enabled: ${ENABLE_TELEMETRY}")

# Define or undefine the macro globally
if(ENABLE_TELEMETRY)
    add_compile_definitions(TELEMETRY_ENABLED)
else()
    # Not defined → disabled
endif()
