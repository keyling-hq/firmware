idf_component_register(
    SRCS "protocol.c" "bridge.pb.c"
    INCLUDE_DIRS "include" "."
    REQUIRES nanopb
    PRIV_REQUIRES typer
)
