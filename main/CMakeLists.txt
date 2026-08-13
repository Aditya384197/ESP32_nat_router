idf_component_register(
    SRCS
        main.c
        router_core.c
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        lwip
)

# Keep the compiler strict. Networking pointer/type warnings must not be
# hidden because undefined behavior in the packet path is a stability risk.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wformat
    -Werror=format
)
