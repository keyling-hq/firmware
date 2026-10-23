from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Result(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    RESULT_OK: _ClassVar[Result]
    RESULT_INVALID: _ClassVar[Result]
    RESULT_FAILED: _ClassVar[Result]
    RESULT_ABORTED: _ClassVar[Result]
RESULT_OK: Result
RESULT_INVALID: Result
RESULT_FAILED: Result
RESULT_ABORTED: Result

class Request(_message.Message):
    __slots__ = ("id", "text", "key_combo", "consumer", "keyboard_report", "strokes", "ping", "get_config", "set_config", "get_device_info", "ota_begin", "ota_data", "ota_end", "ota_abort")
    ID_FIELD_NUMBER: _ClassVar[int]
    TEXT_FIELD_NUMBER: _ClassVar[int]
    KEY_COMBO_FIELD_NUMBER: _ClassVar[int]
    CONSUMER_FIELD_NUMBER: _ClassVar[int]
    KEYBOARD_REPORT_FIELD_NUMBER: _ClassVar[int]
    STROKES_FIELD_NUMBER: _ClassVar[int]
    PING_FIELD_NUMBER: _ClassVar[int]
    GET_CONFIG_FIELD_NUMBER: _ClassVar[int]
    SET_CONFIG_FIELD_NUMBER: _ClassVar[int]
    GET_DEVICE_INFO_FIELD_NUMBER: _ClassVar[int]
    OTA_BEGIN_FIELD_NUMBER: _ClassVar[int]
    OTA_DATA_FIELD_NUMBER: _ClassVar[int]
    OTA_END_FIELD_NUMBER: _ClassVar[int]
    OTA_ABORT_FIELD_NUMBER: _ClassVar[int]
    id: int
    text: Text
    key_combo: KeyCombo
    consumer: Consumer
    keyboard_report: KeyboardReport
    strokes: Strokes
    ping: Ping
    get_config: GetConfig
    set_config: SetConfig
    get_device_info: GetDeviceInfo
    ota_begin: OtaBegin
    ota_data: OtaData
    ota_end: OtaEnd
    ota_abort: OtaAbort
    def __init__(self, id: _Optional[int] = ..., text: _Optional[_Union[Text, _Mapping]] = ..., key_combo: _Optional[_Union[KeyCombo, _Mapping]] = ..., consumer: _Optional[_Union[Consumer, _Mapping]] = ..., keyboard_report: _Optional[_Union[KeyboardReport, _Mapping]] = ..., strokes: _Optional[_Union[Strokes, _Mapping]] = ..., ping: _Optional[_Union[Ping, _Mapping]] = ..., get_config: _Optional[_Union[GetConfig, _Mapping]] = ..., set_config: _Optional[_Union[SetConfig, _Mapping]] = ..., get_device_info: _Optional[_Union[GetDeviceInfo, _Mapping]] = ..., ota_begin: _Optional[_Union[OtaBegin, _Mapping]] = ..., ota_data: _Optional[_Union[OtaData, _Mapping]] = ..., ota_end: _Optional[_Union[OtaEnd, _Mapping]] = ..., ota_abort: _Optional[_Union[OtaAbort, _Mapping]] = ...) -> None: ...

class Text(_message.Message):
    __slots__ = ("text",)
    TEXT_FIELD_NUMBER: _ClassVar[int]
    text: str
    def __init__(self, text: _Optional[str] = ...) -> None: ...

class Strokes(_message.Message):
    __slots__ = ("strokes",)
    STROKES_FIELD_NUMBER: _ClassVar[int]
    strokes: bytes
    def __init__(self, strokes: _Optional[bytes] = ...) -> None: ...

class KeyCombo(_message.Message):
    __slots__ = ("modifiers", "keys")
    MODIFIERS_FIELD_NUMBER: _ClassVar[int]
    KEYS_FIELD_NUMBER: _ClassVar[int]
    modifiers: int
    keys: bytes
    def __init__(self, modifiers: _Optional[int] = ..., keys: _Optional[bytes] = ...) -> None: ...

class Consumer(_message.Message):
    __slots__ = ("usage",)
    USAGE_FIELD_NUMBER: _ClassVar[int]
    usage: int
    def __init__(self, usage: _Optional[int] = ...) -> None: ...

class KeyboardReport(_message.Message):
    __slots__ = ("modifiers", "keys")
    MODIFIERS_FIELD_NUMBER: _ClassVar[int]
    KEYS_FIELD_NUMBER: _ClassVar[int]
    modifiers: int
    keys: bytes
    def __init__(self, modifiers: _Optional[int] = ..., keys: _Optional[bytes] = ...) -> None: ...

class GetConfig(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class SetConfig(_message.Message):
    __slots__ = ("config",)
    CONFIG_FIELD_NUMBER: _ClassVar[int]
    config: Config
    def __init__(self, config: _Optional[_Union[Config, _Mapping]] = ...) -> None: ...

class Ping(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class GetDeviceInfo(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class OtaBegin(_message.Message):
    __slots__ = ("size", "sha256")
    SIZE_FIELD_NUMBER: _ClassVar[int]
    SHA256_FIELD_NUMBER: _ClassVar[int]
    size: int
    sha256: bytes
    def __init__(self, size: _Optional[int] = ..., sha256: _Optional[bytes] = ...) -> None: ...

class OtaData(_message.Message):
    __slots__ = ("offset", "data")
    OFFSET_FIELD_NUMBER: _ClassVar[int]
    DATA_FIELD_NUMBER: _ClassVar[int]
    offset: int
    data: bytes
    def __init__(self, offset: _Optional[int] = ..., data: _Optional[bytes] = ...) -> None: ...

class OtaEnd(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class OtaAbort(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class Config(_message.Message):
    __slots__ = ("layout", "key_press_ms", "key_release_ms", "available_layouts")
    LAYOUT_FIELD_NUMBER: _ClassVar[int]
    KEY_PRESS_MS_FIELD_NUMBER: _ClassVar[int]
    KEY_RELEASE_MS_FIELD_NUMBER: _ClassVar[int]
    AVAILABLE_LAYOUTS_FIELD_NUMBER: _ClassVar[int]
    layout: str
    key_press_ms: int
    key_release_ms: int
    available_layouts: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, layout: _Optional[str] = ..., key_press_ms: _Optional[int] = ..., key_release_ms: _Optional[int] = ..., available_layouts: _Optional[_Iterable[str]] = ...) -> None: ...

class Event(_message.Message):
    __slots__ = ("hello", "credits", "ack", "status", "config", "device_info")
    HELLO_FIELD_NUMBER: _ClassVar[int]
    CREDITS_FIELD_NUMBER: _ClassVar[int]
    ACK_FIELD_NUMBER: _ClassVar[int]
    STATUS_FIELD_NUMBER: _ClassVar[int]
    CONFIG_FIELD_NUMBER: _ClassVar[int]
    DEVICE_INFO_FIELD_NUMBER: _ClassVar[int]
    hello: Hello
    credits: Credits
    ack: Ack
    status: Status
    config: Config
    device_info: DeviceInfo
    def __init__(self, hello: _Optional[_Union[Hello, _Mapping]] = ..., credits: _Optional[_Union[Credits, _Mapping]] = ..., ack: _Optional[_Union[Ack, _Mapping]] = ..., status: _Optional[_Union[Status, _Mapping]] = ..., config: _Optional[_Union[Config, _Mapping]] = ..., device_info: _Optional[_Union[DeviceInfo, _Mapping]] = ...) -> None: ...

class Hello(_message.Message):
    __slots__ = ("protocol_version", "max_frame", "firmware_version", "capabilities")
    PROTOCOL_VERSION_FIELD_NUMBER: _ClassVar[int]
    MAX_FRAME_FIELD_NUMBER: _ClassVar[int]
    FIRMWARE_VERSION_FIELD_NUMBER: _ClassVar[int]
    CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    protocol_version: int
    max_frame: int
    firmware_version: str
    capabilities: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, protocol_version: _Optional[int] = ..., max_frame: _Optional[int] = ..., firmware_version: _Optional[str] = ..., capabilities: _Optional[_Iterable[str]] = ...) -> None: ...

class Credits(_message.Message):
    __slots__ = ("bytes", "reset")
    BYTES_FIELD_NUMBER: _ClassVar[int]
    RESET_FIELD_NUMBER: _ClassVar[int]
    bytes: int
    reset: bool
    def __init__(self, bytes: _Optional[int] = ..., reset: _Optional[bool] = ...) -> None: ...

class Ack(_message.Message):
    __slots__ = ("id", "result")
    ID_FIELD_NUMBER: _ClassVar[int]
    RESULT_FIELD_NUMBER: _ClassVar[int]
    id: int
    result: Result
    def __init__(self, id: _Optional[int] = ..., result: _Optional[_Union[Result, str]] = ...) -> None: ...

class Status(_message.Message):
    __slots__ = ("typing", "usb_mounted", "layout")
    TYPING_FIELD_NUMBER: _ClassVar[int]
    USB_MOUNTED_FIELD_NUMBER: _ClassVar[int]
    LAYOUT_FIELD_NUMBER: _ClassVar[int]
    typing: bool
    usb_mounted: bool
    layout: str
    def __init__(self, typing: _Optional[bool] = ..., usb_mounted: _Optional[bool] = ..., layout: _Optional[str] = ...) -> None: ...

class DeviceInfo(_message.Message):
    __slots__ = ("name", "model", "chip", "flash_size", "uptime_s", "reset_reason", "free_heap", "min_free_heap", "apps", "security_key", "tx_power_dbm", "security")
    NAME_FIELD_NUMBER: _ClassVar[int]
    MODEL_FIELD_NUMBER: _ClassVar[int]
    CHIP_FIELD_NUMBER: _ClassVar[int]
    FLASH_SIZE_FIELD_NUMBER: _ClassVar[int]
    UPTIME_S_FIELD_NUMBER: _ClassVar[int]
    RESET_REASON_FIELD_NUMBER: _ClassVar[int]
    FREE_HEAP_FIELD_NUMBER: _ClassVar[int]
    MIN_FREE_HEAP_FIELD_NUMBER: _ClassVar[int]
    APPS_FIELD_NUMBER: _ClassVar[int]
    SECURITY_KEY_FIELD_NUMBER: _ClassVar[int]
    TX_POWER_DBM_FIELD_NUMBER: _ClassVar[int]
    SECURITY_FIELD_NUMBER: _ClassVar[int]
    name: str
    model: str
    chip: str
    flash_size: int
    uptime_s: int
    reset_reason: str
    free_heap: int
    min_free_heap: int
    apps: _containers.RepeatedCompositeFieldContainer[App]
    security_key: SecurityKey
    tx_power_dbm: int
    security: Security
    def __init__(self, name: _Optional[str] = ..., model: _Optional[str] = ..., chip: _Optional[str] = ..., flash_size: _Optional[int] = ..., uptime_s: _Optional[int] = ..., reset_reason: _Optional[str] = ..., free_heap: _Optional[int] = ..., min_free_heap: _Optional[int] = ..., apps: _Optional[_Iterable[_Union[App, _Mapping]]] = ..., security_key: _Optional[_Union[SecurityKey, _Mapping]] = ..., tx_power_dbm: _Optional[int] = ..., security: _Optional[_Union[Security, _Mapping]] = ...) -> None: ...

class App(_message.Message):
    __slots__ = ("id", "name")
    ID_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    id: str
    name: str
    def __init__(self, id: _Optional[str] = ..., name: _Optional[str] = ...) -> None: ...

class SecurityKey(_message.Message):
    __slots__ = ("status",)
    STATUS_FIELD_NUMBER: _ClassVar[int]
    status: str
    def __init__(self, status: _Optional[str] = ...) -> None: ...

class Security(_message.Message):
    __slots__ = ("secure_boot", "flash_encryption", "key_storage", "signed_updates", "signer", "jtag_disabled", "download_mode")
    SECURE_BOOT_FIELD_NUMBER: _ClassVar[int]
    FLASH_ENCRYPTION_FIELD_NUMBER: _ClassVar[int]
    KEY_STORAGE_FIELD_NUMBER: _ClassVar[int]
    SIGNED_UPDATES_FIELD_NUMBER: _ClassVar[int]
    SIGNER_FIELD_NUMBER: _ClassVar[int]
    JTAG_DISABLED_FIELD_NUMBER: _ClassVar[int]
    DOWNLOAD_MODE_FIELD_NUMBER: _ClassVar[int]
    secure_boot: bool
    flash_encryption: str
    key_storage: str
    signed_updates: bool
    signer: bytes
    jtag_disabled: bool
    download_mode: str
    def __init__(self, secure_boot: _Optional[bool] = ..., flash_encryption: _Optional[str] = ..., key_storage: _Optional[str] = ..., signed_updates: _Optional[bool] = ..., signer: _Optional[bytes] = ..., jtag_disabled: _Optional[bool] = ..., download_mode: _Optional[str] = ...) -> None: ...
