# Resolves + downloads the CEF binary distribution at configure time.
#
# Usage (all optional; everything is inert unless SX_ENABLE_CEF_SPIKE=ON):
#   include(CefSdk)                 # after list(APPEND CMAKE_MODULE_PATH cmake/)
#   sx_setup_cef()                  # -> sx::cef + libcef_dll_wrapper + SX_CEF_RUNTIME_FILES
#
# Strategy: fetch the official cef-builds index.json once (~10 MB), resolve the
# newest STABLE windows64 "minimal" distribution, download (~160 MB) into
# build/_ext/cef, SHA1-verify against the index, extract, cache by filename.
#
# Schema notes (verified empirically against the live index, 2026-08):
#   root.<platform>.versions[] = { cef_version, channel, chromium_version,
#                                  sandbox_compat, files[] }
#   files[] entries carry {name, sha1, size, type} but NO url - the download
#   URL is https://cef-builds.spotifycdn.com/<name> with '+' encoded as %2B.

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
  # root.<platform>.versions[]
  string(JSON _ver ERROR_VARIABLE _e GET "${_json}" "${SX_CEF_PLATFORM}" "versions")
  if(_e)
    message(FATAL_ERROR "cef-builds index: no platform '${SX_CEF_PLATFORM}.versions': ${_e}")
  endif()
  string(JSON _n LENGTH "${_ver}")

  set(_dist_name "")
  set(_dist_sha1 "")
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE 0 ${_last})
    string(JSON _ch GET "${_ver}" "${_i}" "channel")
    if(NOT _ch STREQUAL "stable")
      continue()
    endif()
    string(JSON _files LENGTH "${_ver}" "${_i}" "files")
    math(EXPR _flast "${_files} - 1")
    foreach(_f RANGE 0 ${_flast})
      string(JSON _name GET "${_ver}" "${_i}" "files" "${_f}" "name")
      if(_name MATCHES "_minimal\\.tar\\.bz2$")
        string(JSON _sha1 GET "${_ver}" "${_i}" "files" "${_f}" "sha1")
        set(_dist_name "${_name}")
        set(_dist_sha1 "${_sha1}")
        break()
      endif()
    endforeach()
    if(_dist_name)
      break()
    endif()
  endforeach()

  if(NOT _dist_name)
    message(FATAL_ERROR "cef-builds index: no stable minimal build found")
  endif()

  # '+' must be percent-encoded in the CDN path (verified HTTP 200).
  string(REPLACE "+" "%2B" _dist_url "https://cef-builds.spotifycdn.com/${_dist_name}")

  set(_pkg "${_base}/${_dist_name}")
  set(_sdk "${_base}/sdk")

  if(NOT EXISTS "${_sdk}/CMakeLists.txt")
    if(NOT EXISTS "${_pkg}")
      message(STATUS "Downloading CEF (${_dist_name}, ~160 MB)...")
      file(DOWNLOAD "${_dist_url}" "${_pkg}" SHOW_PROGRESS STATUS _st)
      list(GET _st 0 _code)
      if(NOT _code EQUAL 0)
        message(FATAL_ERROR "CEF download failed: ${_st}")
      endif()
    endif()

    # Integrity gate: match the index-declared SHA1 before extracting.
    file(SHA1 "${_pkg}" _got_sha1)
    if(NOT _got_sha1 STREQUAL _dist_sha1)
      file(REMOVE "${_pkg}")
      message(FATAL_ERROR "CEF distribution SHA1 mismatch:\n  expected ${_dist_sha1}\n  got      ${_got_sha1}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${_pkg}" DESTINATION "${_base}/tmp")
    file(GLOB _one "${_base}/tmp/cef_binary_*")
    list(LENGTH _one _cnt)
    if(NOT _cnt EQUAL 1)
      message(FATAL_ERROR "Unexpected CEF archive layout: ${_one}")
    endif()
    file(RENAME "${_one}" "${_sdk}")
    file(REMOVE_RECURSE "${_base}/tmp")
    message(STATUS "CEF SDK ready: ${_dist_name}")
  endif()

  add_library(sx::cef UNKNOWN IMPORTED)
  set_target_properties(sx::cef PROPERTIES
    IMPORTED_LOCATION             "${_sdk}/Release/libcef.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_sdk}")

  # -- C++ wrapper (fixes LNK2019 on CefInitialize/CreateBrowser/etc.) -----
  # libcef.lib exports only the flat C API; the C++ layer (CefInitialize,
  # CefExecuteProcess, CefShutdown, CefDoMessageLoopWork,
  # CefBrowserHost::CreateBrowser, ...) is implemented in the wrapper sources
  # under <sdk>/libcef_dll and must be compiled into the link. We build just
  # the wrapper instead of add_subdirectory(<sdk>) because the SDK's own
  # CMake also declares cefclient/cefsimple targets we neither want nor can
  # configure headlessly here.
  #
  # NOTE: the wrapper MUST use the same MSVC runtime as its consumers
  # (MultiThreadedDLL == /MD, matching spike_cef) or the link fails with
  # LNK2038 runtime-mismatch before symbol resolution is even reached.
  file(GLOB_RECURSE _wrapper_src CONFIGURE_DEPENDS "${_sdk}/libcef_dll/*.cc")
  add_library(libcef_dll_wrapper STATIC ${_wrapper_src})
  target_include_directories(libcef_dll_wrapper PUBLIC "${_sdk}")
  target_compile_features(libcef_dll_wrapper PUBLIC cxx_std_17)
  if(MSVC)
    set_target_properties(libcef_dll_wrapper PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  endif()

  # Every sx::cef consumer now pulls the wrapper automatically.
  target_link_libraries(sx::cef INTERFACE libcef_dll_wrapper)

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
