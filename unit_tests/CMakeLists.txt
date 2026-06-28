add_executable(unit_tests
  test_execution_tree.cpp
  test_fully_persistent_array.cpp
  test_module_utils.cpp
  test_thread_utils.cpp
  test_trampoline.cpp
  test_module_manager.cpp
  test_breakpoint_manager.cpp
  test_thread_manager.cpp
  test_memory_manager.cpp
  test_event_log.cpp
  test_snapshot_log.cpp
  "${CMAKE_SOURCE_DIR}/src/globals.cpp"
  "${CMAKE_SOURCE_DIR}/src/utils/module_utils.cpp"
  "${CMAKE_SOURCE_DIR}/src/utils/thread_utils.cpp"
  "${CMAKE_SOURCE_DIR}/src/utils/trampoline.cpp"
  "${CMAKE_SOURCE_DIR}/src/managers/module_manager.cpp"
  "${CMAKE_SOURCE_DIR}/src/managers/breakpoint_manager.cpp"
  "${CMAKE_SOURCE_DIR}/src/managers/thread_manager.cpp"
  "${CMAKE_SOURCE_DIR}/src/managers/memory_manager.cpp"
  "${CMAKE_SOURCE_DIR}/src/windows/nt_wrappers.cpp"
  "${CMAKE_SOURCE_DIR}/src/Events/shared_library_events.cpp"
  "${CMAKE_SOURCE_DIR}/src/Events/thread_events.cpp"
  "${CMAKE_SOURCE_DIR}/src/event_log.cpp"
  "${CMAKE_SOURCE_DIR}/src/snapshot_log.cpp"
)

target_include_directories(unit_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/utils"
  "${CMAKE_SOURCE_DIR}/src/windows"
  "${CMAKE_SOURCE_DIR}/src/managers"
  "${CMAKE_SOURCE_DIR}/src/Events"
)
target_compile_definitions(unit_tests PRIVATE ZYDIS_STATIC_BUILD)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain Zydis ntdll)

set_target_properties(unit_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
