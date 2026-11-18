set(TDESKTOP_TG_WS_PROXY_FILE "" CACHE FILEPATH "Optional local copy of the pinned TgWsProxy release executable.")

set(proxy_dir "${CMAKE_CURRENT_BINARY_DIR}/tgwsproxy")
set(proxy_exe "${proxy_dir}/TgWsProxy_windows.exe")
set(proxy_notice "${res_loc}/winrc/tgwsproxy-license.txt")
set(proxy_rc "${proxy_dir}/TgWsProxy.rc")
set(proxy_fetch "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch_tg_ws_proxy.cmake")
file(GENERATE OUTPUT "${proxy_rc}" CONTENT
    "TGWSPROXY RCDATA \"${proxy_exe}\"\nTGWSPROXY_LICENSE RCDATA \"${proxy_notice}\"\n")
add_custom_command(
    OUTPUT "${proxy_exe}"
    COMMAND ${CMAKE_COMMAND}
        "-Doutput=${proxy_exe}"
        "-Dlocal_file=${TDESKTOP_TG_WS_PROXY_FILE}"
        -P "${proxy_fetch}"
    DEPENDS "${proxy_fetch}" ${TDESKTOP_TG_WS_PROXY_FILE}
    VERBATIM)
add_custom_target(Telegram_tgwsproxy DEPENDS "${proxy_exe}")
add_dependencies(Telegram Telegram_tgwsproxy)
set_source_files_properties("${proxy_rc}" PROPERTIES
    OBJECT_DEPENDS "${proxy_exe};${proxy_notice}")
target_sources(Telegram PRIVATE "${proxy_rc}")
