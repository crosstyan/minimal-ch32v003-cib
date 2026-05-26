cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_CURRENT_LIST_DIR}/cmake/riscv-none-elf-gcc.cmake"
        CACHE FILEPATH "RISC-V bare-metal toolchain file")
endif()

project(ch32v003_cib_blink LANGUAGES C CXX ASM)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

foreach(_lang C CXX ASM)
    set(CMAKE_${_lang}_FLAGS_RELWITHDEBINFO
        "-Os -g3 -DNDEBUG"
        CACHE STRING "RelWithDebInfo ${_lang} flags" FORCE)
endforeach()

set(CH32FUN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ch32fun")
set(CIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/compile-time-init-build")

if(NOT EXISTS "${CH32FUN_SOURCE_DIR}/ch32fun/ch32fun.c")
    message(FATAL_ERROR
        "ch32fun submodule is missing. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CIB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "compile-time-init-build submodule is missing. Run: git submodule update --init --recursive")
endif()

add_subdirectory("${CIB_SOURCE_DIR}" EXCLUDE_FROM_ALL)

add_library(ch32v003_flags INTERFACE)
target_compile_options(
    ch32v003_flags
    INTERFACE
        -march=rv32ec
        -mabi=ilp32e
        -misa-spec=2.2
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g3>
        $<$<NOT:$<CONFIG:Debug>>:-Os>
        $<$<NOT:$<CONFIG:Debug>>:-g3>
        -msmall-data-limit=8
        -mno-save-restore
        -ffunction-sections
        -fdata-sections
        -fmessage-length=0
        -fno-tree-loop-distribute-patterns
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -Wall
        -Werror=double-promotion
        -Werror=float-conversion
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)
target_link_options(
    ch32v003_flags
    INTERFACE
        -march=rv32ec
        -mabi=ilp32e
        -misa-spec=2.2
        -nostdlib
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
        -Wl,--fatal-warnings)

set(CH32FUN_DIR "${CH32FUN_SOURCE_DIR}/ch32fun")
set(CH32FUN_LINKER_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generated_ch32v003.ld")

add_custom_command(
    OUTPUT "${CH32FUN_LINKER_SCRIPT}"
    COMMAND
        "${CMAKE_C_COMPILER}" -E -P -x c
        -DTARGET_MCU=CH32V003
        -DMCU_PACKAGE=1
        -DTARGET_MCU_LD=0
        -DTARGET_MCU_MEMORY_SPLIT=0
        "${CH32FUN_DIR}/ch32fun.ld"
        -o "${CH32FUN_LINKER_SCRIPT}"
    DEPENDS "${CH32FUN_DIR}/ch32fun.ld"
    VERBATIM)
add_custom_target(ch32fun_linker_script DEPENDS "${CH32FUN_LINKER_SCRIPT}")

add_library(ch32fun STATIC "${CH32FUN_DIR}/ch32fun.c")
target_link_libraries(ch32fun PUBLIC ch32v003_flags)
target_compile_definitions(ch32fun PUBLIC CH32V003=1 CPLUSPLUS=1)
target_include_directories(
    ch32fun
    PUBLIC
        "${CH32FUN_DIR}"
        "${CH32FUN_SOURCE_DIR}/extralibs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(FIRMWARE_TARGET firmware)
add_executable(${FIRMWARE_TARGET} src/main.cpp src/system_tick.cpp)
set_target_properties(
    ${FIRMWARE_TARGET}
    PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        SUFFIX ".elf"
        LINK_DEPENDS "${CH32FUN_LINKER_SCRIPT}")
add_dependencies(${FIRMWARE_TARGET} ch32fun_linker_script)
target_link_libraries(
    ${FIRMWARE_TARGET}
    PRIVATE
        ch32fun
        cib_flow
        cib_nexus
        ch32v003_flags
        gcc)
target_include_directories(${FIRMWARE_TARGET} PRIVATE src)
target_compile_definitions(${FIRMWARE_TARGET} PRIVATE SIMULATE_FREESTANDING=1)
target_link_options(
    ${FIRMWARE_TARGET}
    PRIVATE
        "-Wl,-Map=$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.map"
        "-T${CH32FUN_LINKER_SCRIPT}")

add_custom_command(
    TARGET ${FIRMWARE_TARGET}
    POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${FIRMWARE_TARGET}>"
            "$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.bin"
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${FIRMWARE_TARGET}>"
            "$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.hex"
    COMMAND "${CMAKE_OBJDUMP}" -S "$<TARGET_FILE:${FIRMWARE_TARGET}>"
            > "$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.lst"
    COMMAND
        "${CMAKE_COMMAND}"
        "-DELF=$<TARGET_FILE:${FIRMWARE_TARGET}>"
        "-DMAP_FILE=$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.map"
        "-DSIZE_TOOL=${CMAKE_SIZE}"
        "-DNM_TOOL=${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-nm"
        "-DFLASH_LIMIT=16384"
        "-DRAM_LIMIT=2048"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_firmware.cmake"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${FIRMWARE_TARGET}>"
    VERBATIM)

add_custom_target(size
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${FIRMWARE_TARGET}>"
    DEPENDS ${FIRMWARE_TARGET}
    VERBATIM)

set(FIRMWARE_ELF "$<TARGET_FILE:${FIRMWARE_TARGET}>")
set(FIRMWARE_BIN "$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${PROJECT_NAME}.bin")

find_program(PROBE_RS probe-rs)
set(PROBE_RS_CHIP "CH32V003" CACHE STRING "probe-rs chip name")
set(PROBE_RS_DOWNLOAD_ARGS "--verify" CACHE STRING "Extra probe-rs download arguments")
separate_arguments(PROBE_RS_DOWNLOAD_ARGS_LIST NATIVE_COMMAND "${PROBE_RS_DOWNLOAD_ARGS}")

if(PROBE_RS)
    add_custom_target(
        flash_probe_rs
        COMMAND
            "${PROBE_RS}" download
            --chip "${PROBE_RS_CHIP}"
            ${PROBE_RS_DOWNLOAD_ARGS_LIST}
            "${FIRMWARE_ELF}"
        DEPENDS ${FIRMWARE_TARGET}
        USES_TERMINAL
        VERBATIM)
    add_custom_target(flash DEPENDS flash_probe_rs)
else()
    message(WARNING "probe-rs was not found; the flash target will not be available")
endif()

find_program(MAKE_EXECUTABLE NAMES gmake make)
find_program(MINICHLINK_PROGRAM minichlink)
set(MINICHLINK_SOURCE_DIR "${CH32FUN_SOURCE_DIR}/minichlink")
set(MINICHLINK_VENDOR_EXECUTABLE "${MINICHLINK_SOURCE_DIR}/minichlink")

if(MAKE_EXECUTABLE AND EXISTS "${MINICHLINK_SOURCE_DIR}/Makefile")
    add_custom_target(
        minichlink_tool
        COMMAND "${MAKE_EXECUTABLE}" -C "${MINICHLINK_SOURCE_DIR}" minichlink
        BYPRODUCTS "${MINICHLINK_VENDOR_EXECUTABLE}"
        USES_TERMINAL
        VERBATIM)
    set(MINICHLINK_DEFAULT "${MINICHLINK_VENDOR_EXECUTABLE}")
elseif(MINICHLINK_PROGRAM)
    set(MINICHLINK_DEFAULT "${MINICHLINK_PROGRAM}")
endif()

set(MINICHLINK "${MINICHLINK_DEFAULT}" CACHE FILEPATH "minichlink executable")
set(MINICHLINK_WRITE_SECTION "flash" CACHE STRING "minichlink memory section")

if(MINICHLINK)
    add_custom_target(
        flash_minichlink
        COMMAND
            "${MINICHLINK}" -w "${FIRMWARE_BIN}" "${MINICHLINK_WRITE_SECTION}" -b
        DEPENDS ${FIRMWARE_TARGET}
        USES_TERMINAL
        VERBATIM)
    if(MINICHLINK STREQUAL MINICHLINK_VENDOR_EXECUTABLE AND TARGET minichlink_tool)
        add_dependencies(flash_minichlink minichlink_tool)
    endif()
else()
    message(WARNING "minichlink was not found and cannot be built; flash_minichlink will not be available")
endif()

find_program(OPENOCD NAMES wch-openocd openocd)
set(OPENOCD_CONFIG_FILES "target/wch-riscv.cfg" CACHE STRING "Semicolon-separated OpenOCD config files")
set(OPENOCD_EXTRA_ARGS "" CACHE STRING "Extra OpenOCD arguments")
separate_arguments(OPENOCD_EXTRA_ARGS_LIST NATIVE_COMMAND "${OPENOCD_EXTRA_ARGS}")

set(OPENOCD_FLASH_ARGS ${OPENOCD_EXTRA_ARGS_LIST})
foreach(_openocd_config IN LISTS OPENOCD_CONFIG_FILES)
    list(APPEND OPENOCD_FLASH_ARGS -f "${_openocd_config}")
endforeach()
list(APPEND OPENOCD_FLASH_ARGS
    -c init
    -c halt
    -c "program ${FIRMWARE_ELF} verify"
    -c wlink_reset_resume
    -c exit)

if(OPENOCD)
    add_custom_target(
        flash_openocd
        COMMAND "${OPENOCD}" ${OPENOCD_FLASH_ARGS}
        DEPENDS ${FIRMWARE_TARGET}
        USES_TERMINAL
        VERBATIM)
else()
    message(WARNING "OpenOCD was not found; flash_openocd will not be available")
endif()
