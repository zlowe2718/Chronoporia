cmake_minimum_required(VERSION 3.21)
project(chronoporia LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR
    "chronoporia uses Windows-only NT debugging APIs, the MSVC DIA SDK, and "
    "FASM-assembled shellcode, and cannot be built on this platform.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHRONOPORIA_TESTS "Build Catch2 and the unit_tests target" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# chronoporia, plugins, and tests used the Unicode character set in their
# original vcxproj configs, which is what makes the Win32 TCHAR-style APIs
# (e.g. LoadLibrary) resolve to their *W overloads. This must NOT be applied
# globally: Catch2's bundled main() picks an entry point based on _UNICODE
# (wmain vs main), so leaking this define into unit_tests/FetchContent deps
# breaks the link.
set(CHRONOPORIA_UNICODE_DEFINITIONS UNICODE _UNICODE)

include(FetchContent)

# ---- Zydis (disassembler) ----
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  zydis
  GIT_REPOSITORY https://github.com/zyantific/zydis.git
  GIT_TAG v4.1.1
  GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(zydis)

# ---- Quill (logging) ----
FetchContent_Declare(
  quill
  GIT_REPOSITORY https://github.com/odygrd/quill.git
  GIT_TAG v12.0.0
)
FetchContent_MakeAvailable(quill)

# ---- argparse (command-line argument parsing) ----
FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/p-ranav/argparse.git
  GIT_TAG v3.1
)
FetchContent_MakeAvailable(argparse)

# ---- Catch2 (unit_tests) ----
if(CHRONOPORIA_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# ---- FASM-assembled shellcode object ----
find_program(FASM_EXECUTABLE fasm REQUIRED)

# Assembles src/utils/asm/unload_dll.asm and exposes the resulting object
# file's path via ${out_var}, for use as a prebuilt object source.
function(chronoporia_add_unload_dll_obj out_var)
  set(asm_src "${CMAKE_SOURCE_DIR}/src/utils/asm/unload_dll.asm")
  set(obj_out "${CMAKE_CURRENT_BINARY_DIR}/unload_dll.obj")
  add_custom_command(
    OUTPUT "${obj_out}"
    COMMAND "${FASM_EXECUTABLE}" "${asm_src}" "${obj_out}"
    DEPENDS "${asm_src}"
    COMMENT "Assembling unload_dll.asm with FASM"
    VERBATIM
  )
  set_source_files_properties("${obj_out}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set(${out_var} "${obj_out}" PARENT_SCOPE)
endfunction()

set(CHRONOPORIA_SOURCES
  src/resources.rc
  src/Events/shared_library_events.cpp
  src/Events/thread_events.cpp
  src/event_log.cpp
  src/execution_state_machine/execution_phase/debugger_phase.cpp
  src/execution_state_machine/execution_phase/process_start_phase.cpp
  src/execution_state_machine/execution_phase/reconstruction_phase.cpp
  src/execution_state_machine/execution_phase/record_phase.cpp
  src/execution_state_machine/execution_phase/time_restore_phase.cpp
  src/execution_state_machine/state_machine.cpp
  src/execution_state_machine/transitions/transitions_box.cpp
  src/globals.cpp
  src/main.cpp
  src/managers/breakpoint_manager.cpp
  src/managers/memory_manager.cpp
  src/managers/module_manager.cpp
  src/managers/plugin_manager.cpp
  src/managers/thread_manager.cpp
  src/managers/trampoline_manager.cpp
  src/snapshot_log.cpp
  src/utils/module_utils.cpp
  src/utils/shellcode.cpp
  src/utils/thread_utils.cpp
  src/utils/trampoline.cpp
  src/windows/nt_undocumented.cpp
  src/windows/nt_wrappers.cpp
)

chronoporia_add_unload_dll_obj(CHRONOPORIA_UNLOAD_DLL_OBJ)

add_executable(chronoporia ${CHRONOPORIA_SOURCES} ${CHRONOPORIA_UNLOAD_DLL_OBJ})

target_include_directories(chronoporia PRIVATE
  src
  src/windows
  src/Events
  src/execution_state_machine
  src/execution_state_machine/execution_phase
  src/execution_state_machine/transitions
  src/utils
  src/managers
)

target_compile_definitions(chronoporia PRIVATE
  ZYDIS_STATIC_BUILD
  NOMINMAX
  ${CHRONOPORIA_UNICODE_DEFINITIONS}
  $<$<CONFIG:Release>:QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO>
  $<$<CONFIG:Debug>:QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG>
)

target_link_libraries(chronoporia PRIVATE Zydis quill::quill argparse::argparse ntdll)

set_target_properties(chronoporia PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_custom_command(TARGET chronoporia POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:chronoporia>" "${CMAKE_SOURCE_DIR}/test_scripts/python/"
)

# add_subdirectory(plugins)
add_subdirectory(tests)

if(CHRONOPORIA_TESTS)
  add_subdirectory(unit_tests)
endif()