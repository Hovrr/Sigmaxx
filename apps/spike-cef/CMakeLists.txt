# Spike B compiles only when explicitly enabled; default builds stay green.
if(NOT SX_ENABLE_CEF_SPIKE)
  message(STATUS "Spike B (CEF-OSR) disabled; enable with -DSX_ENABLE_CEF_SPIKE=ON")
  return()
endif()

include(CefSdk)          # provided via root CMAKE_MODULE_PATH
sx_setup_cef()

add_executable(spike_cef WIN32
  src/main.cpp
)

set_target_properties(spike_cef PROPERTIES
  OUTPUT_NAME "SigmaxxPhase0-CefOsr"
  MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin-cef")

target_include_directories(spike_cef PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SX_CEF_ROOT})

target_compile_features(spike_cef PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(spike_cef PRIVATE /W4 /permissive- /utf-8)
endif()

target_compile_definitions(spike_cef PRIVATE UNICODE _UNICODE)

target_link_libraries(spike_cef PRIVATE
  sx_eal
  sx::cef
  user32 gdi32 ole32 advapi32 uuid psapi)

# Ship the CEF runtime beside the exe.
foreach(_f ${SX_CEF_RUNTIME_FILES})
  add_custom_command(TARGET spike_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_f}" "$<TARGET_FILE_DIR:spike_cef>")
endforeach()
