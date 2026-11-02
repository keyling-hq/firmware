if(CONFIG_USB_HID_TINYUSB)
    set(srcs "usb_hid_tinyusb.c" "usb_hid_msc_class.c")
else()
    set(srcs "usb_hid_log.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_system esp_hw_support soc
)

# TinyUSB's MSC class, enabled for this component only: the Kconfig option would also build
# esp_tinyusb's storage layer, which implements the MSC callbacks itself.
if(CONFIG_USB_HID_TINYUSB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_TINYUSB_MSC_ENABLED=1 CONFIG_TINYUSB_MSC_BUFSIZE=512)
endif()
