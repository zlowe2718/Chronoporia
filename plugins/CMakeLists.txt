# Shared interface for breakpoint plugins: gives every plugin subdirectory
# access to base_breakpoint_plugin.h without duplicating the include path.
add_library(breakpoint_plugin_base INTERFACE)
target_include_directories(breakpoint_plugin_base INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# Each subfolder here is an independent plugin with its own CMakeLists.txt
# (and its own dependencies), producing its own DLL.
#
# By default every plugin folder is built. To build only a subset, pass
# -DCHRONOPORIA_PLUGINS="cpp;python" (semicolon-separated folder names).
set(CHRONOPORIA_PLUGINS "all" CACHE STRING
  "Semicolon-separated list of plugin folder names to build, or \"all\" to build every plugin under plugins/")

file(GLOB _plugin_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(_dir ${_plugin_dirs})
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
    if(CHRONOPORIA_PLUGINS STREQUAL "all" OR _dir IN_LIST CHRONOPORIA_PLUGINS)
      add_subdirectory(${_dir})
    endif()
  endif()
endforeach()
