idf_component_register(
    SRCS "protocol.c" "bridge.pb.c"
    INCLUDE_DIRS "include" "."
    REQUIRES nanopb
    PRIV_REQUIRES typer esp_app_format esp_system fw_info logbuf ota_update
)
