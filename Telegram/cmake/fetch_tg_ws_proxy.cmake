set(proxy_hash 454205cbcf76f333934108d893b58a528373bce8096624e27f0b6dc16eea660f)
set(proxy_url "https://github.com/Flowseal/tg-ws-proxy/releases/download/v1.11.4/TgWsProxy_windows.exe")

if (local_file)
    file(SHA256 "${local_file}" actual_hash)
    if (NOT actual_hash STREQUAL proxy_hash)
        message(FATAL_ERROR "Local TgWsProxy binary does not match the pinned Flowseal release.")
    endif()
    file(COPY_FILE "${local_file}" "${output}" ONLY_IF_DIFFERENT)
else()
    file(DOWNLOAD "${proxy_url}" "${output}.download"
        EXPECTED_HASH "SHA256=${proxy_hash}"
        TLS_VERIFY ON TIMEOUT 180)
    file(RENAME "${output}.download" "${output}")
endif()
