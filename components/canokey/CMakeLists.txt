set(CK ${CMAKE_CURRENT_LIST_DIR}/canokey-core)
set(CK_CRYPTO ${CK}/canokey-crypto)
set(CK_GEN ${CMAKE_CURRENT_LIST_DIR}/generated)

file(GLOB CK_SRCS
    ${CK}/src/*.c
    ${CK}/applets/admin/*.c
    ${CK}/applets/ctap/*.c
    ${CK}/applets/oath/*.c
    ${CK}/applets/openpgp/*.c
    ${CK}/applets/pass/*.c
    ${CK}/applets/piv/*.c
    ${CK_CRYPTO}/src/*.c
)
list(FILTER CK_SRCS EXCLUDE REGEX "ml-dsa-65\\.c$|ml-kem-768\\.c$")
list(APPEND CK_SRCS
    ${CK}/interfaces/USB/class/ctaphid/ctaphid.c
    ${CK}/tinycbor/src/cborencoder.c
    ${CK}/tinycbor/src/cborparser.c
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Generated headers canokey-core expects from its own CMake build.
    file(MAKE_DIRECTORY ${CK_GEN})
    file(WRITE ${CK_GEN}/firmware-version.h
    "#pragma once
    #define CANOKEY_CTAPHID_DEVICE_VERSION_0 0
    #define CANOKEY_CTAPHID_DEVICE_VERSION_1 1
    #define CANOKEY_CTAPHID_DEVICE_VERSION_2 0
    #define CANOKEY_PIV_VERSION_0 0
    #define CANOKEY_PIV_VERSION_1 0
    #define CANOKEY_PIV_VERSION_2 0
    #define CANOKEY_OATH_VERSION_0 0
    #define CANOKEY_OATH_VERSION_1 0
    #define CANOKEY_OATH_VERSION_2 0
    #define FIRMWARE_VERSION 1
    #define CANOKEY_USB_BCD_DEVICE 0x0100
    ")
    file(WRITE ${CK_GEN}/canokey-core-git-rev.h "#pragma once\n#define CANOKEY_CORE_GIT_REV \"keyling\"\n")
    file(WRITE ${CK_GEN}/tinycbor-export.h "#ifndef CBOR_API\n#define CBOR_API\n#endif\n")
    file(WRITE ${CK_GEN}/tinycbor-version.h
        "#define TINYCBOR_VERSION_MAJOR 7\n#define TINYCBOR_VERSION_MINOR 0\n#define TINYCBOR_VERSION_PATCH 0\n")

    idf_build_get_property(python PYTHON)
    execute_process(
        COMMAND ${python} ${CK}/scripts/gen_ctap_get_info.py
            --headers ${CK}/applets/ctap/ctap-internal.h ${CK}/applets/ctap/cose-key.h ${CK_GEN}/firmware-version.h
            --restrict-algorithms 1
            --credential-id-size 70
            --output ${CK_GEN}/ctap_get_info_cbor.inc
        RESULT_VARIABLE gen_result
    )
    if(NOT gen_result EQUAL 0)
        message(FATAL_ERROR "canokey: generating ctap_get_info_cbor.inc failed")
    endif()
endif()

idf_component_register(
    SRCS ${CK_SRCS}
         "port/canokey_port.c"
         "port/ctap_request.c"
         "port/ctaphid_glue.c"
         "port/crypto_stubs.c"
         "port/dev_attestation.c"
         "port/monocypher/monocypher.c"
         "port/monocypher/monocypher-ed25519.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "port"
        "port/monocypher"
        ${CK}/include
        ${CK}/applets/ctap
        ${CK}/interfaces/USB/class/ctaphid
        ${CK}/interfaces/USB/class/kbdhid
        ${CK}/interfaces/USB/core/inc
        ${CK}/interfaces/USB/device
        ${CK}/tinycbor/src
        ${CK_CRYPTO}/include
        ${CK_GEN}
    PRIV_REQUIRES esp_driver_gpio esp_partition esp_timer joltwallet__littlefs mbedtls nvs_flash usb_hid
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ENABLE_NFC=0
    ENABLE_APPLET_NDEF=0
    ENABLE_PASS=0
    ENABLE_IFACE_CTAPHID=1
    ENABLE_IFACE_WEBUSB=0
    ENABLE_IFACE_CCID=0
    ENABLE_IFACE_KBDHID=0
    CTAP_RESTRICT_ALGORITHMS=1
    USE_MBEDCRYPTO
    MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
    MBEDTLS_ECP_DP_ED25519=MBEDTLS_ECP_DP_NONE
    CBOR_NO_FLOATING_POINT
    CBOR_STATIC_DEFINE
    ESP_LITTLEFS_RAW_API
    DEBUG_OUTPUT
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-format -Wno-error)

# port/ctap_request.c peeks at each CTAP command before canokey processes it;
# port/canokey_port.c replaces the user presence wait.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=ctap_process_cbor_stream_with_src"
    "-Wl,--wrap=ctap_process_cbor_stream_source_with_src"
    "-Wl,--wrap=wait_for_user_presence"
)
