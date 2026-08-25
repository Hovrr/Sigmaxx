# Resolves + downloads the CEF binary distribution at configure time.
#
# Usage (all optional; everything is inert unless SX_ENABLE_CEF_SPIKE=ON):
#   include(CefSdk)                 # after list(APPEND CMAKE_MODULE_PATH cmake/)
#   sx_setup_cef()                  # -> sx::cef target + SX_CEF_RUNTIME_FILES
#
# Strategy: fetch the official cef-builds index.json once, resolve the newest
# STABLE windows64 "minimal" distribution via string(JSON ...), download and
# extract into build/_ext/cef, cache by distribution filename. No hand-pinned
# hashes to rot.

set(SX_CEF_PLATFORM "windows64" CACHE STRING "cef-builds platform key")

function(sx_setup_cef)
  if(TARGET sx::cef)
    return()
  endif()

  set(_base "${CMAKE_BINARY_DIR}/_ext/cef")
  set(_idx  "${_base}/index.json")
  set(_url_idx "https://cef-builds.spotifycdn.com/index.json")

  if(NOT EXISTS "${_idx}")
    file(DOWNLOAD "${_url_idx}" "${_idx}" SHOW_PROGRESS STATUS _st)
    list(GET _st 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "Cannot fetch cef-builds index: ${_st}")
    endif()
  endif()

  file(READ "${_idx}" _json LIMIT_MB 64)
  string(JSON _arr ERROR_VARIABLE _e GET "${_json}" "${SX_CEF_PLATFORM}")
  if(_e)
    message(FATAL_ERROR "cef-builds index: no platform ${SX_CEF_PLATFORM}: ${_e}")
  endif()
  string(JSON _n LENGTH "${_arr}")

  set(_dist_url "")
  set(_dist_name "")
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE 0 ${_last})
    string(JSON _ch GET "${_arr}" "${_i}" "channel")
    if(NOT _ch STREQUAL "stable")
      continue()
    endif()
    string(JSON _files LENGTH "${_arr}" "${_i}" "files")
    math(EXPR _flast "${_files} - 1")
    foreach(_f RANGE 0 ${_flast})
      string(JSON _name GET "${_arr}" "${_i}" "files" "${_f}" "name")
      if(_name MATCHES "_minimal\\.tar\\.bz2$")
        string(JSON _url GET "${_arr}" "${_i}" "files" "${_f}" "url")
        set(_dist_url "${_url}")
        set(_dist_name "${_name}")
        break()
      endif()
    endforeach()
    if(_dist_url)
      break()
    endif()
  endforeach()

  if(NOT _dist_url)
    message(FATAL_ERROR "cef-builds index: no stable minimal build found")
  endif()

  set(_pkg   "${_base}/${_dist_name}")
  set(_sdk   "${_base}/sdk")

  if(NOT EXISTS "${_sdk}/CMakeLists.txt")
    if(NOT EXISTS "${_pkg}")
      message(STATUS "Downloading CEF ${_dist_name} (~90 MB)...")
      file(DOWNLOAD "${_dist_url}" "${_pkg}" SHOW_PROGRESS STATUS _st)
      list(GET _st 0 _code)
      if(NOT _code EQUAL 0)
        message(FATAL_ERROR "CEF download failed: ${_st}")
      endif()
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_pkg}" DESTINATION "${_base}/tmp")
    file(GLOB _one "${_base}/tmp/cef_binary_*")
    list(LENGTH _one _cnt)
    if(NOT _cnt EQUAL 1)
      message(FATAL_ERROR "Unexpected CEF archive layout: ${_one}")
    endif()
    file(RENAME "${_one}" "${_sdk}")
    file(REMOVE_RECURSE "${_base}/tmp")
  endif()

  add_library(sx::cef UNKNOWN IMPORTED)
  set_target_properties(sx::cef PROPERTIES
    IMPORTED_LOCATION             "${_sdk}/Release/libcef.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_sdk}")

  set(SX_CEF_ROOT       "${_sdk}"       PARENT_SCOPE)
  set(SX_CEF_RUNTIME_FILES
      "${_sdk}/Release/libcef.dll"
      "${_sdk}/Release/chrome_elf.dll"
      "${_sdk}/Release/d3dcompiler_47.dll"
      "${_sdk}/Release/libEGL.dll"
      "${_sdk}/Release/libGLESv2.dll"
      "${_sdk}/Release/vk_swiftshader.dll"
      "${_sdk}/Resources/icudtl.dat"
      "${_sdk}/Resources/resources.pak"
      "${_sdk}/Resources/snapshot_blob.bin"
      "${_sdk}/Resources/v8_context_snapshot.bin"
      PARENT_SCOPE)
endfunction()
