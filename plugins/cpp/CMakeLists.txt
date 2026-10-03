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

# dia2.h pulls in ATL headers (atlbase.h, atlcomcli.h) for CComPtr. These ship
# with the MSVC toolset, not the Windows SDK, so MSVC's own include search
# finds them automatically -- but clang/clang-cl and clangd need the path
# spelled out explicitly. Locate the newest atlmfc/include and let callers
# override with -DATLMFC_INCLUDE_DIR=<path>.
file(GLOB _atlmfc_candidates
  "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/atlmfc/include"
  "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/atlmfc/include"
)
if(_atlmfc_candidates)
  list(SORT _atlmfc_candidates COMPARE NATURAL ORDER DESCENDING)
  list(GET _atlmfc_candidates 0 _atlmfc_default)
endif()
set(ATLMFC_INCLUDE_DIR "${_atlmfc_default}" CACHE PATH "Path to the MSVC atlmfc/include directory (contains atlbase.h, atlcomcli.h)")

if(NOT EXISTS "${ATLMFC_INCLUDE_DIR}/atlcomcli.h")
  message(FATAL_ERROR
    "ATL headers not found (looked for atlcomcli.h under '${ATLMFC_INCLUDE_DIR}'). "
    "Set -DATLMFC_INCLUDE_DIR=<path to atlmfc/include under your Visual Studio's MSVC toolset>.")
endif()

add_library(cpp_breakpoint_plugin SHARED
  cpp_breakpoint_plugin.cpp
  pdb.cpp
  utils.cpp
  compiler.cpp
  branch_instrumentation.cpp
  predecessor_analysis.cpp
)

target_include_directories(cpp_breakpoint_plugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${DIASDK_DIR}/include"
  "${ATLMFC_INCLUDE_DIR}"
)

target_link_directories(cpp_breakpoint_plugin PRIVATE "${DIASDK_DIR}/lib/amd64" "${ATLMFC_INCLUDE_DIR}/../lib/x64")
target_link_libraries(cpp_breakpoint_plugin PRIVATE breakpoint_plugin_base diaguids dbghelp Zydis)
target_compile_definitions(cpp_breakpoint_plugin PRIVATE ${CHRONOPORIA_UNICODE_DEFINITIONS} ZYDIS_STATIC_BUILD)

target_compile_options(cpp_breakpoint_plugin PRIVATE 
  $<$<CONFIG:Release>:/Zi>
  $<$<CONFIG:Release>:/fsanitize=address>
)
target_link_options(cpp_breakpoint_plugin PRIVATE
  $<$<CONFIG:Release>:/DEBUG>
  $<$<CONFIG:Release>:/OPT:REF>
  $<$<CONFIG:Release>:/OPT:ICF>
)

set_target_properties(cpp_breakpoint_plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_custom_command(TARGET cpp_breakpoint_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:cpp_breakpoint_plugin>" "${CMAKE_SOURCE_DIR}/test_scripts/python/"
)
