# The DIA SDK ships inside a Visual Studio install, not the Windows SDK, so
# there's no find_package() for it. Locate the newest installed copy and let
# callers override with -DDIASDK_DIR=<path containing include/dia2.h>.
file(GLOB _diasdk_candidates
  "C:/Program Files/Microsoft Visual Studio/*/*/DIA SDK"
  "C:/Program Files (x86)/Microsoft Visual Studio/*/*/DIA SDK"
)
if(_diasdk_candidates)
  list(SORT _diasdk_candidates COMPARE NATURAL ORDER DESCENDING)
  list(GET _diasdk_candidates 0 _diasdk_default)
endif()
set(DIASDK_DIR "${_diasdk_default}" CACHE PATH "Path to the MSVC DIA SDK (the folder containing include/ and lib/)")

if(NOT EXISTS "${DIASDK_DIR}/include/dia2.h")
  message(FATAL_ERROR
    "DIA SDK not found (looked for include/dia2.h under '${DIASDK_DIR}'). "
    "Set -DDIASDK_DIR=<path to the DIA SDK that ships with your Visual Studio install>.")
endif()

add_library(plugins SHARED
  cpp/cpp_breakpoint_plugin.cpp
)

target_include_directories(plugins PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${DIASDK_DIR}/include"
)

target_link_directories(plugins PRIVATE "${DIASDK_DIR}/lib/amd64")
target_link_libraries(plugins PRIVATE diaguids)
target_compile_definitions(plugins PRIVATE ${CHRONOPORIA_UNICODE_DEFINITIONS})

set_target_properties(plugins PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_custom_command(TARGET plugins POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:plugins>" "${CMAKE_SOURCE_DIR}/test_scripts/python/"
)
