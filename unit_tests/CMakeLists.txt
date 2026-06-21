add_executable(unit_tests
  test_execution_tree.cpp
)

target_include_directories(unit_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/utils")
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain)

set_target_properties(unit_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
