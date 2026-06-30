idf_component_register(SRCS
                           "open62541.c"
                           "open62541_esp_time.c"
                           "arch/common/eventloop_common.c"
                           "arch/common/timer.c"
                           "arch/lwip/eventloop_lwip.c"
                           "arch/lwip/eventloop_lwip_tcp.c"
                           "arch/lwip/eventloop_lwip_udp.c"
                       INCLUDE_DIRS
                           "."
                           "arch/common"
                           "arch/lwip"
                           "deps"
                       PRIV_REQUIRES lwip esp_timer)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UA_ARCHITECTURE_FREERTOS
    UA_ARCHITECTURE_LWIP
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-missing-field-initializers
    -Wno-format
    -Wno-parentheses
    -Wno-incompatible-pointer-types
)
