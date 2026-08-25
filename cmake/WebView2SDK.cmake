# Downloads the Microsoft.Web.WebView2 SDK (NuGet) at configure time and exposes:
#   * target sx::webview2            (UNKNOWN IMPORTED, headers + loader lib)
#   * function sx_webview2_copy_runtime(<target>)  -> copies WebView2Loader.dll beside
#     the exe when the DLL loader variant was used instead of the static one.
#
# Why not a vcpkg port? The upstream port surface has churned; pinning the NuGet
# version here gives bit-identical SDK bits in CI and locally.

set(SX_WEBVIEW2_SDK_VERSION "1.0.2210.55" CACHE STRING "Microsoft.Web.WebView2 SDK version")

function(sx_setup_webview2)
  if(TARGET sx::webview2)
    return()
  endif()

  set(_base    "${CMAKE_BINARY_DIR}/_ext/webview2")
  set(_sdk_dir "${_base}/sdk")
  set(_inc     "${_sdk_dir}/build/native/include")
  set(_libdir  "${_sdk_dir}/build/native/x64")

  if(NOT EXISTS "${_inc}/WebView2.h")
    set(_pkg "${_base}/microsoft.web.webview2.${SX_WEBVIEW2_SDK_VERSION}.nupkg")
    file(DOWNLOAD
      "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${SX_WEBVIEW2_SDK_VERSION}/microsoft.web.webview2.${SX_WEBVIEW2_SDK_VERSION}.nupkg"
      "${_pkg}"
      SHOW_PROGRESS
      STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "Failed to download WebView2 SDK ${SX_WEBVIEW2_SDK_VERSION}: ${_dl_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_pkg}" DESTINATION "${_sdk_dir}")
  endif()

  set(_static "${_libdir}/WebView2LoaderStatic.lib")
  set(_dllimp "${_libdir}/WebView2Loader.lib")

  if(EXISTS "${_static}")
    set(_loader "${_static}")
    set(_runtime_dll "")
  elseif(EXISTS "${_dllimp}")
    set(_loader "${_dllimp}")
    set(_runtime_dll "${_libdir}/WebView2Loader.dll")
  else()
    message(FATAL_ERROR "WebView2 loader libraries not found under ${_libdir}")
  endif()

  add_library(sx::webview2 UNKNOWN IMPORTED)
  set_target_properties(sx::webview2 PROPERTIES
    IMPORTED_LOCATION             "${_loader}"
    INTERFACE_INCLUDE_DIRECTORIES "${_inc}")

  set(SX_WEBVIEW2_RUNTIME_DLL "${_runtime_dll}" PARENT_SCOPE)
endfunction()

function(sx_webview2_copy_runtime tgt)
  sx_setup_webview2()
  if(SX_WEBVIEW2_RUNTIME_DLL)
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${SX_WEBVIEW2_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${tgt}>")
  endif()
endfunction()

sx_setup_webview2()
