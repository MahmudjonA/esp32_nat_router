file(GLOB_RECURSE UTLGBOT_SRCS
    "src/*.c"
    "src/*.cpp"
)

# ❌ ЖЁСТКО ВЫКИДЫВАЕМ bundled mbedtls
list(FILTER UTLGBOT_SRCS EXCLUDE REGEX "multihttpsclient/mbedtls")


idf_component_register(
    SRCS
        ${UTLGBOT_SRCS}
    INCLUDE_DIRS
        "src"
        "src/utility"
        "src/utility/jsmn"
        "src/utility/multihttpsclient"

    REQUIRES
        esp-tls
        esp_http_client
        mbedtls 
        nvs_flash
        esp_timer        # 🔥 ВАЖНО
        freertos
)

# 🔥 КРИТИЧНО: отключаем Werror ТОЛЬКО для uTLGBotLib и ТОЛЬКО для C/C++
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-error=format-truncation
    -Wno-format-truncation
)

# 🔥 ДОПОЛНИТЕЛЬНО: явно для C++
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=format-truncation>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-format-truncation>
)