cmake_minimum_required(VERSION 3.24)

project(Sigmaxx
  VERSION   0.1.0
  LANGUAGES CXX
  DESCRIPTION "Sigmaxx - hybrid vector/raster graphic design suite")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# vcpkg manifest mode is driven by the toolchain file; nothing to do here.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WebView2SDK)

add_subdirectory(libs/sx_eal)
add_subdirectory(apps/spike-webview2)
