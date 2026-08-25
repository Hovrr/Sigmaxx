# The WebView2 SDK target is created by the root list file (module WebView2SDK).
if(NOT TARGET sx::webview2)
  message(FATAL_ERROR "sx::webview2 not defined - root CMakeLists.txt must include(WebView2SDK)")
endif()

add_executable(spike_webview2 WIN32
  src/main.cpp
)

set_target_properties(spike_webview2 PROPERTIES
  OUTPUT_NAME "SigmaxxPhase0-WebView2"
  MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

target_include_directories(spike_webview2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(spike_webview2 PRIVATE cxx_std_20)

# Win32 GUI code uses the W family exclusively; make the macros match so
# IDC_ARROW / IDI_APPLICATION resolve to wide variants.
target_compile_definitions(spike_webview2 PRIVATE UNICODE _UNICODE)

if(MSVC)
  target_compile_options(spike_webview2 PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(spike_webview2 PRIVATE
  sx_eal
  sx::webview2
  user32 gdi32 ole32 oleaut32 advapi32 uuid psapi)

sx_webview2_copy_runtime(spike_webview2)
