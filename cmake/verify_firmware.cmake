if(NOT DEFINED ELF OR NOT EXISTS "${ELF}")
    message(FATAL_ERROR "ELF was not provided or does not exist: ${ELF}")
endif()

if(NOT DEFINED FLASH_LIMIT)
    set(FLASH_LIMIT 16384)
endif()
if(NOT DEFINED RAM_LIMIT)
    set(RAM_LIMIT 2048)
endif()

execute_process(
    COMMAND "${SIZE_TOOL}" "${ELF}"
    RESULT_VARIABLE _size_result
    OUTPUT_VARIABLE _size_out
    ERROR_VARIABLE _size_err)
if(NOT _size_result EQUAL 0)
    message(FATAL_ERROR "size failed:\n${_size_err}")
endif()

if(NOT _size_out MATCHES "\n[ \t]*([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+")
    message(FATAL_ERROR "Unable to parse size output:\n${_size_out}")
endif()

set(_text "${CMAKE_MATCH_1}")
set(_data "${CMAKE_MATCH_2}")
set(_bss "${CMAKE_MATCH_3}")
math(EXPR _flash "${_text} + ${_data}")
math(EXPR _ram "${_data} + ${_bss}")

if(_flash GREATER FLASH_LIMIT)
    message(FATAL_ERROR
        "Flash footprint ${_flash} bytes exceeds ${FLASH_LIMIT} bytes")
endif()
if(_ram GREATER RAM_LIMIT)
    message(FATAL_ERROR
        "RAM footprint ${_ram} bytes exceeds ${RAM_LIMIT} bytes")
endif()

execute_process(
    COMMAND "${NM_TOOL}" -C "${ELF}"
    RESULT_VARIABLE _nm_result
    OUTPUT_VARIABLE _nm_out
    ERROR_VARIABLE _nm_err)
if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed:\n${_nm_err}")
endif()

if(DEFINED MAP_FILE AND EXISTS "${MAP_FILE}")
    file(READ "${MAP_FILE}" _map_out)
    string(CONCAT _banned_archive_regex
        "(libc(_nano)?\\.a|libstdc\\+\\+(_nano)?\\.a|libsupc\\+\\+\\.a|"
        "libm\\.a|libnosys\\.a)")
    if(_map_out MATCHES "${_banned_archive_regex}")
        string(REGEX MATCHALL "[^\n]*${_banned_archive_regex}[^\n]*"
            _archive_matches "${_map_out}")
        list(REMOVE_DUPLICATES _archive_matches)
        list(JOIN _archive_matches "\n" _archive_matches_text)
        message(FATAL_ERROR
            "Banned C/C++ runtime archive linked. Use ch32fun's tiny runtime/printf instead:\n${_archive_matches_text}")
    endif()
endif()

string(CONCAT _banned_regex
    "(^|[\n \t])("
    "_malloc_r|malloc|_calloc_r|calloc|_realloc_r|realloc|_free_r|free|"
    "_sbrk|_sbrk_r|"
    "operator new|operator delete|"
    "std::basic_string|std::__cxx11::basic_string|std::pmr::|"
    "std::vector|std::locale|std::iostream|std::cout|std::cerr|"
    "fprintf|vfprintf|fiprintf|vfiprintf|"
    "_vfprintf_r|_vfiprintf_r|_printf_common|_printf_i|"
    "__sf|__sfp|__sinit|__sfvwrite_r|"
    "__addsf3|__subsf3|__mulsf3|__divsf3|__floatsisf|__fixsfsi|"
    "__extendsfdf2|__truncdfsf2|__adddf3|__subdf3|__muldf3|__divdf3|"
    "__floatsidf|__fixdfsi|sqrtf|sinf|cosf|tanf|atanf|powf|floorf|ceilf|roundf"
    ")([\n \t(]|$)")

if(_nm_out MATCHES "${_banned_regex}")
    string(REGEX MATCHALL "([^\n]*(${_banned_regex})[^\n]*)" _matches "${_nm_out}")
    list(JOIN _matches "\n" _matches_text)
    message(FATAL_ERROR
        "Banned runtime symbol found. Heap, printf, float helpers, and heavy std runtime are not allowed:\n${_matches_text}")
endif()

message(STATUS "Footprint check passed: flash=${_flash}/${FLASH_LIMIT} bytes, ram=${_ram}/${RAM_LIMIT} bytes")
