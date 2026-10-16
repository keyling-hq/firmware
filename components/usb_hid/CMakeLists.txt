if(CONFIG_USB_HID_TINYUSB)
    set(srcs "usb_hid_tinyusb.c")
else()
    set(srcs "usb_hid_log.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_system esp_hw_support soc
)
