chronoporia_add_unload_dll_obj(TESTS_UNLOAD_DLL_OBJ)

add_executable(tests
  test_unload_dll.cpp
  ${TESTS_UNLOAD_DLL_OBJ}
)

target_compile_definitions(tests PRIVATE ${CHRONOPORIA_UNICODE_DEFINITIONS})

set_target_properties(tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
