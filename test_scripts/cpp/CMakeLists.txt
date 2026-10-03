file(GLOB TEST_SCRIPT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Assembly listings for the test scripts land here, one .cod/.s per source.
# The compiler will not create the directory itself, so make it up front for
# every configuration this generator might build.
set(TEST_SCRIPT_ASM_ROOT "${CMAKE_BINARY_DIR}/asm/test_scripts")
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    file(MAKE_DIRECTORY "${TEST_SCRIPT_ASM_ROOT}/${cfg}")
  endforeach()
  set(TEST_SCRIPT_ASM_DIR "${TEST_SCRIPT_ASM_ROOT}/$<CONFIG>")
else()
  file(MAKE_DIRECTORY "${TEST_SCRIPT_ASM_ROOT}/${CMAKE_BUILD_TYPE}")
  set(TEST_SCRIPT_ASM_DIR "${TEST_SCRIPT_ASM_ROOT}/${CMAKE_BUILD_TYPE}")
endif()

foreach(test_script_src ${TEST_SCRIPT_SOURCES})
  get_filename_component(test_script_name ${test_script_src} NAME_WE)

  add_executable(${test_script_name} ${test_script_src})

  set_target_properties(${test_script_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/test_scripts")

  if(MSVC)
    # /FAsc = assembly listing with interleaved source and machine code, which
    # is what makes these listings useful for lining up bytes with the
    # debugger's disassembly. The trailing slash makes /Fa treat the argument
    # as a directory; with /FAsc the listings are written as <source>.cod.
    target_compile_options(${test_script_name} PRIVATE
      /FAsc
      "/Fa${TEST_SCRIPT_ASM_DIR}/"
    )
  else()
    # -save-temps=obj keeps the .s alongside the object instead of clobbering
    # files in the build root when two sources share a basename.
    target_compile_options(${test_script_name} PRIVATE -save-temps=obj)
  endif()
endforeach()
