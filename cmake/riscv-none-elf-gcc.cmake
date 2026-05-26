set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX
    "/Users/crosstyan/External/opt/xpack-riscv-none-elf-gcc-15.2.0-1"
    CACHE PATH "xPack RISC-V GCC install prefix")
set(TOOLCHAIN_TRIPLE "riscv-none-elf" CACHE STRING "RISC-V toolchain triple")

set(TOOLCHAIN_BIN_DIR "${TOOLCHAIN_PREFIX}/bin")

set(_TOOLCHAIN_C_COMPILER "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc")
set(_TOOLCHAIN_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-g++")

if(NOT EXISTS "${_TOOLCHAIN_C_COMPILER}")
    message(FATAL_ERROR "C compiler not found: ${_TOOLCHAIN_C_COMPILER}")
endif()
if(NOT EXISTS "${_TOOLCHAIN_CXX_COMPILER}")
    message(FATAL_ERROR "C++ compiler not found: ${_TOOLCHAIN_CXX_COMPILER}")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    file(GLOB _TOOLCHAIN_GCC_VERSION_DIRS
        LIST_DIRECTORIES true
        "${TOOLCHAIN_PREFIX}/libexec/gcc/${TOOLCHAIN_TRIPLE}/*")
    if(NOT _TOOLCHAIN_GCC_VERSION_DIRS)
        message(FATAL_ERROR "GCC frontend directory not found under ${TOOLCHAIN_PREFIX}/libexec")
    endif()
    list(SORT _TOOLCHAIN_GCC_VERSION_DIRS COMPARE NATURAL ORDER DESCENDING)
    list(GET _TOOLCHAIN_GCC_VERSION_DIRS 0 _TOOLCHAIN_GCC_VERSION_DIR)

    set(_TOOLCHAIN_FRONTEND_DIR "${CMAKE_BINARY_DIR}/.xpack-gcc-frontends")
    file(MAKE_DIRECTORY "${_TOOLCHAIN_FRONTEND_DIR}")
    foreach(_frontend cc1 cc1plus)
        file(COPY "${_TOOLCHAIN_GCC_VERSION_DIR}/${_frontend}"
             DESTINATION "${_TOOLCHAIN_FRONTEND_DIR}")
    endforeach()

    function(_make_xpack_wrapper output compiler)
        set(_wrapper "${CMAKE_BINARY_DIR}/.${output}")
        file(WRITE "${_wrapper}"
"#!/bin/bash
export DYLD_LIBRARY_PATH=\"${TOOLCHAIN_PREFIX}/libexec\${DYLD_LIBRARY_PATH:+:\${DYLD_LIBRARY_PATH}}\"

args=()
while [[ \$# -gt 0 ]]; do
    case \"\$1\" in
        -target)
            shift
            [[ \$# -gt 0 ]] && shift
            ;;
        --target=*)
            shift
            ;;
        *)
            args+=(\"\$1\")
            shift
            ;;
    esac
done

exec \"${compiler}\" \"-B${_TOOLCHAIN_FRONTEND_DIR}/\" \"\${args[@]}\"
")
        file(CHMOD "${_wrapper}"
             PERMISSIONS
                 OWNER_READ OWNER_WRITE OWNER_EXECUTE
                 GROUP_READ GROUP_EXECUTE
                 WORLD_READ WORLD_EXECUTE)
        set(${output} "${_wrapper}" PARENT_SCOPE)
    endfunction()

    _make_xpack_wrapper(_TOOLCHAIN_C_WRAPPER "${_TOOLCHAIN_C_COMPILER}")
    _make_xpack_wrapper(_TOOLCHAIN_CXX_WRAPPER "${_TOOLCHAIN_CXX_COMPILER}")

    set(CMAKE_C_COMPILER "${_TOOLCHAIN_C_WRAPPER}" CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${_TOOLCHAIN_CXX_WRAPPER}" CACHE FILEPATH "C++ compiler")
    set(CMAKE_ASM_COMPILER "${_TOOLCHAIN_C_WRAPPER}" CACHE FILEPATH "ASM compiler")
else()
    set(CMAKE_C_COMPILER "${_TOOLCHAIN_C_COMPILER}" CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${_TOOLCHAIN_CXX_COMPILER}" CACHE FILEPATH "C++ compiler")
    set(CMAKE_ASM_COMPILER "${_TOOLCHAIN_C_COMPILER}" CACHE FILEPATH "ASM compiler")
endif()

set(CMAKE_AR
    "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-ar"
    CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB
    "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-ranlib"
    CACHE FILEPATH "Ranlib")
set(CMAKE_OBJCOPY
    "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-objcopy"
    CACHE FILEPATH "Objcopy")
set(CMAKE_OBJDUMP
    "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-objdump"
    CACHE FILEPATH "Objdump")
set(CMAKE_SIZE
    "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-size"
    CACHE FILEPATH "Size")

set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_PREFIX}/${TOOLCHAIN_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
