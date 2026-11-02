#include <stdio.h>
#include <string.h>
#include "usb_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/msc/msc.h"
#include "device/usbd_pvt.h"

static const char *TAG = "usb_hid";

#define USB_PID                 0x4F01
/* The drive mode has its own product ID, so hosts keep separate driver bindings for each set. */
#define USB_PID_DISK            0x4F02
/* Changes whenever the interface set does, so hosts don't reuse cached driver bindings. */
#define USB_BCD_DEVICE          0x0201
#define HID_EP_SIZE             16
#define HID_READY_TIMEOUT_MS    100

#define FIDO_POLL_MS            5

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_FIDO,
    ITF_NUM_CCID,
    ITF_NUM_TOTAL,
};

/* TinyUSB numbers HID instances in descriptor order. */
enum {
    HID_INSTANCE_KEYBOARD = 0,
    HID_INSTANCE_FIDO,
};

#define EPNUM_HID       0x81
#define EPNUM_FIDO_OUT  0x02
#define EPNUM_FIDO_IN   0x82
#define EPNUM_CCID_OUT  0x03
#define EPNUM_CCID_IN   0x83
#define EPNUM_CCID_INT  0x84
#define EPNUM_MSC_OUT   0x03
#define EPNUM_MSC_IN    0x83
#define MSC_EP_SIZE     64

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID,
    STRID_FIDO,
    STRID_CCID,
    STRID_DISK,
    STRID_COUNT,
};

/*
 * USB CCID 1.1 interface with one slot, T=1 and extended APDUs. Byte layout as in canokey-core's
 * descriptor. The interrupt endpoint tells the host when the card is removed or inserted: hosts
 * don't poll for it.
 */
#define CCID_INT_EP_SIZE    8
#define CCID_INT_INTERVAL   16
#define TUD_CCID_DESC_LEN   (9 + 54 + 7 + 7 + 7)
#define TUD_CCID_DESCRIPTOR(itf, stridx, epout, epin, epint, epsize, max_message, max_ifsd)                       \
    9, TUSB_DESC_INTERFACE, itf, 0, 3, TUSB_CLASS_SMART_CARD, 0, 0, stridx,                                       \
    54, 0x21, U16_TO_U8S_LE(0x0110), 0, 0x07, U32_TO_U8S_LE(0x00000002), U32_TO_U8S_LE(4000),                     \
    U32_TO_U8S_LE(4000), 0, U32_TO_U8S_LE(307200), U32_TO_U8S_LE(307200), 0, U32_TO_U8S_LE(max_ifsd),             \
    U32_TO_U8S_LE(0), U32_TO_U8S_LE(0), U32_TO_U8S_LE(0x000400FE), U32_TO_U8S_LE(max_message), 0xFF, 0xFF,       \
    U16_TO_U8S_LE(0), 0, 1,                                                                                       \
    7, TUSB_DESC_ENDPOINT, epin, TUSB_XFER_BULK, U16_TO_U8S_LE(epsize), 0,                                        \
    7, TUSB_DESC_ENDPOINT, epout, TUSB_XFER_BULK, U16_TO_U8S_LE(epsize), 0,                                       \
    7, TUSB_DESC_ENDPOINT, epint, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(CCID_INT_EP_SIZE), CCID_INT_INTERVAL

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE,
    REPORT_ID_CONSUMER,
};

static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
};

static const uint8_t s_fido_report_desc[] = {
    TUD_HID_REPORT_DESC_FIDO_U2F(USB_HID_FIDO_REPORT_SIZE),
};

#define CONFIG_HEAD_LEN     (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define CONFIG_TOTAL_LEN    (CONFIG_HEAD_LEN + TUD_CCID_DESC_LEN)
#define CONFIG_DISK_LEN     (CONFIG_HEAD_LEN + TUD_MSC_DESC_LEN)

#define CONFIG_HEAD(total_len)                                                                                    \
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, total_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),               \
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc), EPNUM_HID,      \
                       HID_EP_SIZE, 1),                                                                           \
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_FIDO, STRID_FIDO, HID_ITF_PROTOCOL_NONE, sizeof(s_fido_report_desc),          \
                             EPNUM_FIDO_OUT, EPNUM_FIDO_IN, USB_HID_FIDO_REPORT_SIZE, FIDO_POLL_MS)

static const uint8_t s_config_card[] = {
    CONFIG_HEAD(CONFIG_TOTAL_LEN),
    TUD_CCID_DESCRIPTOR(ITF_NUM_CCID, STRID_CCID, EPNUM_CCID_OUT, EPNUM_CCID_IN, EPNUM_CCID_INT, USB_HID_CCID_PACKET_SIZE,
                        USB_HID_CCID_MAX_MESSAGE, USB_HID_CCID_MAX_MESSAGE - 10),
};

/* The endpoint budget has no room for both: the drive takes the smartcard's place. */
static const uint8_t s_config_disk[] = {
    CONFIG_HEAD(CONFIG_DISK_LEN),
    TUD_MSC_DESCRIPTOR(ITF_NUM_CCID, STRID_DISK, EPNUM_MSC_OUT, EPNUM_MSC_IN, MSC_EP_SIZE),
};

/* TinyUSB keeps pointers to these and reads them at each enumeration. */
static uint8_t s_config_desc[CONFIG_TOTAL_LEN > CONFIG_DISK_LEN ? CONFIG_TOTAL_LEN : CONFIG_DISK_LEN];

static tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD_DEVICE,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static char s_serial[13];

static const char *s_strings[STRID_COUNT] = {
    [STRID_LANGID] = "\x09\x04",
    [STRID_MANUFACTURER] = "Keyling",
    [STRID_PRODUCT] = "Keyling",
    [STRID_SERIAL] = s_serial,
    [STRID_HID] = "Keyboard",
    [STRID_FIDO] = "Security key",
    [STRID_CCID] = "Smartcard",
    [STRID_DISK] = "Drive",
};

static SemaphoreHandle_t s_lock;
static volatile uint8_t s_leds;
static usb_hid_mount_cb_t s_mount_cb;
static void *s_mount_ctx;
static usb_hid_fido_rx_cb_t s_fido_rx_cb;
static void *s_fido_rx_ctx;

static void usb_event_cb(tinyusb_event_t *event, void *arg)
{
    bool mounted = event->id == TINYUSB_EVENT_ATTACHED;
    if (!mounted) {
        s_leds = 0;
    }
    if (s_mount_cb) {
        s_mount_cb(mounted, s_mount_ctx);
    }
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == HID_INSTANCE_FIDO ? s_fido_report_desc : s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (instance == HID_INSTANCE_FIDO) {
        if (bufsize == USB_HID_FIDO_REPORT_SIZE && s_fido_rx_cb) {
            s_fido_rx_cb(buffer, s_fido_rx_ctx);
        }
        return;
    }
    if (report_id == REPORT_ID_KEYBOARD && report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
        s_leds = buffer[0];
    }
}

/* ---- CCID: an application class driver, TinyUSB has none ------------------ */

static uint8_t s_ccid_ep_in;
static uint8_t s_ccid_ep_out;
static uint8_t s_ccid_ep_int;
static CFG_TUSB_MEM_ALIGN uint8_t s_ccid_int_buf[CCID_INT_EP_SIZE];
static CFG_TUSB_MEM_ALIGN uint8_t s_ccid_rx_buf[USB_HID_CCID_PACKET_SIZE];
static usb_hid_ccid_rx_cb_t s_ccid_rx_cb;
static usb_hid_ccid_tx_done_cb_t s_ccid_tx_done_cb;
static void *s_ccid_ctx;

static void driver_init(void)
{
}

static bool driver_deinit(void)
{
    return true;
}

static void ccid_reset(uint8_t rhport)
{
    s_ccid_ep_in = 0;
    s_ccid_ep_out = 0;
    s_ccid_ep_int = 0;
}

static uint16_t ccid_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    TU_VERIFY(itf->bInterfaceClass == TUSB_CLASS_SMART_CARD, 0);
    uint8_t const *p = (uint8_t const *)itf;
    uint16_t len = 0;
    do {
        len += tu_desc_len(p);
        p = tu_desc_next(p);
    } while (len < max_len && tu_desc_type(p) != TUSB_DESC_ENDPOINT);
    TU_ASSERT(usbd_open_edpt_pair(rhport, p, 2, TUSB_XFER_BULK, &s_ccid_ep_out, &s_ccid_ep_in), 0);
    len += 2 * sizeof(tusb_desc_endpoint_t);
    p = tu_desc_next(tu_desc_next(p));
    if (len < max_len && tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        TU_ASSERT(ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT && usbd_edpt_open(rhport, ep), 0);
        s_ccid_ep_int = ep->bEndpointAddress;
        len += sizeof(tusb_desc_endpoint_t);
    }
    TU_ASSERT(usbd_edpt_xfer(rhport, s_ccid_ep_out, s_ccid_rx_buf, sizeof(s_ccid_rx_buf), false), 0);
    return len;
}

#define CCID_REQUEST_ABORT  0x01

/* ABORT is acknowledged; the PC_to_RDR_Abort that follows on the bulk pipe is the card's business. */
static bool ccid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS || request->bRequest != CCID_REQUEST_ABORT) {
        return false;
    }
    if (stage == CONTROL_STAGE_SETUP) {
        return tud_control_status(rhport, request);
    }
    return true;
}

static bool ccid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    if (ep_addr == s_ccid_ep_out) {
        if (result == XFER_RESULT_SUCCESS && s_ccid_rx_cb) {
            s_ccid_rx_cb(s_ccid_rx_buf, xferred_bytes, s_ccid_ctx);
        }
        return usbd_edpt_xfer(rhport, s_ccid_ep_out, s_ccid_rx_buf, sizeof(s_ccid_rx_buf), false);
    }
    if (ep_addr == s_ccid_ep_in && s_ccid_tx_done_cb) {
        s_ccid_tx_done_cb(s_ccid_ctx);
    }
    return true;
}


/* ---- Drive: USB mass storage, bulk-only transport with a read-only SCSI disk ---- */

#define MSC_CBW_SIGNATURE   0x43425355
#define MSC_CSW_SIGNATURE   0x53425355
#define MSC_CBW_LEN         31
#define MSC_CSW_LEN         13
#define MSC_REQUEST_RESET   0xFF
#define MSC_REQUEST_MAX_LUN 0xFE

#define SCSI_TEST_UNIT_READY        0x00
#define SCSI_REQUEST_SENSE          0x03
#define SCSI_INQUIRY                0x12
#define SCSI_MODE_SENSE_6           0x1A
#define SCSI_START_STOP_UNIT        0x1B
#define SCSI_PREVENT_ALLOW_REMOVAL  0x1E
#define SCSI_READ_FORMAT_CAPACITIES 0x23
#define SCSI_READ_CAPACITY_10       0x25
#define SCSI_READ_10                0x28
#define SCSI_WRITE_10               0x2A
#define SCSI_VERIFY_10              0x2F
#define SCSI_SYNC_CACHE_10          0x35
#define SCSI_MODE_SENSE_10          0x5A

#define SENSE_NOT_READY             0x02
#define SENSE_ILLEGAL_REQUEST       0x05
#define SENSE_DATA_PROTECT          0x07
#define ASC_INVALID_COMMAND         0x20
#define ASC_LBA_OUT_OF_RANGE        0x21
#define ASC_WRITE_PROTECTED         0x27
#define ASC_MEDIUM_NOT_PRESENT      0x3A

typedef enum {
    MSC_STAGE_CMD,
    MSC_STAGE_DATA_IN,
    MSC_STAGE_DATA_OUT,
    MSC_STAGE_STATUS,
    MSC_STAGE_NEED_RESET,
} msc_stage_t;

static const usb_hid_disk_t *volatile s_disk;
static volatile bool s_disk_ejected;
static uint8_t s_msc_ep_in;
static uint8_t s_msc_ep_out;
static msc_stage_t s_msc_stage;
static CFG_TUSB_MEM_ALIGN uint8_t s_msc_cbw[MSC_EP_SIZE];
static CFG_TUSB_MEM_ALIGN uint8_t s_msc_csw[MSC_CSW_LEN];
static CFG_TUSB_MEM_ALIGN uint8_t s_msc_buf[USB_HID_DISK_BLOCK_SIZE];
static uint32_t s_msc_tag;
static uint32_t s_msc_expected;     /* dCBWDataTransferLength */
static uint32_t s_msc_done;         /* data bytes moved so far */
static uint8_t s_msc_status;
static uint32_t s_msc_lba;          /* next block of a READ(10) */
static uint32_t s_msc_blocks;       /* blocks of it still to read */
static uint8_t s_sense_key;
static uint8_t s_sense_asc;

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t get_le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static bool msc_receive_cbw(uint8_t rhport)
{
    s_msc_stage = MSC_STAGE_CMD;
    return usbd_edpt_xfer(rhport, s_msc_ep_out, s_msc_cbw, sizeof(s_msc_cbw), false);
}

static bool msc_send_csw(uint8_t rhport)
{
    s_msc_stage = MSC_STAGE_STATUS;
    uint32_t residue = s_msc_expected > s_msc_done ? s_msc_expected - s_msc_done : 0;
    put_le32(s_msc_csw, MSC_CSW_SIGNATURE);
    put_le32(s_msc_csw + 4, s_msc_tag);
    put_le32(s_msc_csw + 8, residue);
    s_msc_csw[12] = s_msc_status;
    return usbd_edpt_xfer(rhport, s_msc_ep_in, s_msc_csw, MSC_CSW_LEN, false);
}

/* Data phase over: a host that expected more data gets a stall first and the status after it clears. */
static bool msc_finish(uint8_t rhport, bool data_in)
{
    s_msc_stage = MSC_STAGE_STATUS;
    if (data_in && s_msc_done < s_msc_expected) {
        usbd_edpt_stall(rhport, s_msc_ep_in);
        return true;
    }
    return msc_send_csw(rhport);
}

static void msc_fail(uint8_t key, uint8_t asc)
{
    s_msc_status = 1;
    s_sense_key = key;
    s_sense_asc = asc;
}

static bool msc_send_data(uint8_t rhport, uint32_t len)
{
    uint32_t left = s_msc_expected - s_msc_done;
    if (len > left) {
        len = left;
    }
    if (len == 0) {
        return msc_finish(rhport, true);
    }
    s_msc_stage = MSC_STAGE_DATA_IN;
    return usbd_edpt_xfer(rhport, s_msc_ep_in, s_msc_buf, (uint16_t)len, false);
}

static bool msc_read_next(uint8_t rhport)
{
    const usb_hid_disk_t *disk = s_disk;
    if (s_msc_blocks == 0 || s_msc_done >= s_msc_expected || !disk) {
        return msc_finish(rhport, true);
    }
    disk->read(s_msc_lba++, s_msc_buf, disk->ctx);
    s_msc_blocks--;
    return msc_send_data(rhport, USB_HID_DISK_BLOCK_SIZE);
}

/* Fills s_msc_buf for commands with a fixed reply and returns its length; 0 for unknown ones. */
static uint32_t msc_reply(const uint8_t *cmd, uint32_t block_count)
{
    uint8_t *b = s_msc_buf;
    memset(b, 0, 36);
    switch (cmd[0]) {
    case SCSI_INQUIRY:
        b[1] = 0x80;                    /* removable */
        b[2] = 0x04;                    /* SPC-2 */
        b[3] = 0x02;
        b[4] = 36 - 5;
        memcpy(b + 8, "Keyling ", 8);
        memcpy(b + 16, "Drive           ", 16);
        memcpy(b + 32, "1.0 ", 4);
        return 36;
    case SCSI_REQUEST_SENSE:
        b[0] = 0x70;
        b[2] = s_sense_key;
        b[7] = 10;
        b[12] = s_sense_asc;
        s_sense_key = 0;
        s_sense_asc = 0;
        return 18;
    case SCSI_MODE_SENSE_6:
        b[0] = 3;
        b[2] = 0x80;                    /* write protected */
        return 4;
    case SCSI_MODE_SENSE_10:
        b[1] = 6;
        b[3] = 0x80;
        return 8;
    case SCSI_READ_CAPACITY_10:
        put_be32(b, block_count - 1);
        put_be32(b + 4, USB_HID_DISK_BLOCK_SIZE);
        return 8;
    case SCSI_READ_FORMAT_CAPACITIES:
        b[3] = 8;
        put_be32(b + 4, block_count);
        put_be32(b + 8, USB_HID_DISK_BLOCK_SIZE);
        b[8] = 0x02;                    /* formatted media */
        return 12;
    }
    return 0;
}

static bool msc_command(uint8_t rhport, uint32_t len)
{
    const uint8_t *cbw = s_msc_cbw;
    if (len != MSC_CBW_LEN || get_le32(cbw) != MSC_CBW_SIGNATURE) {
        s_msc_stage = MSC_STAGE_NEED_RESET;
        usbd_edpt_stall(rhport, s_msc_ep_in);
        usbd_edpt_stall(rhport, s_msc_ep_out);
        return true;
    }
    s_msc_tag = get_le32(cbw + 4);
    s_msc_expected = get_le32(cbw + 8);
    s_msc_done = 0;
    s_msc_status = 0;
    s_msc_blocks = 0;
    bool data_in = (cbw[12] & 0x80) != 0;
    const uint8_t *cmd = cbw + 15;
    const usb_hid_disk_t *disk = s_disk;
    bool ready = disk && !s_disk_ejected;

    switch (cmd[0]) {
    case SCSI_TEST_UNIT_READY:
        if (!ready) {
            msc_fail(SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        }
        break;
    case SCSI_START_STOP_UNIT:
        if (cmd[4] & 0x02) {            /* load or eject */
            s_disk_ejected = !(cmd[4] & 0x01);
        }
        break;
    case SCSI_PREVENT_ALLOW_REMOVAL:
    case SCSI_VERIFY_10:
    case SCSI_SYNC_CACHE_10:
        break;
    case SCSI_READ_10: {
        uint32_t lba = get_be32(cmd + 2);
        uint32_t count = ((uint32_t)cmd[7] << 8) | cmd[8];
        if (!ready) {
            msc_fail(SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        } else if (lba + count > disk->block_count) {
            msc_fail(SENSE_ILLEGAL_REQUEST, ASC_LBA_OUT_OF_RANGE);
        } else {
            s_msc_lba = lba;
            s_msc_blocks = count;
            return msc_read_next(rhport);
        }
        break;
    }
    case SCSI_WRITE_10:
        msc_fail(SENSE_DATA_PROTECT, ASC_WRITE_PROTECTED);
        break;
    default: {
        uint32_t reply = disk ? msc_reply(cmd, disk->block_count) : 0;
        if (reply) {
            return msc_send_data(rhport, reply);
        }
        msc_fail(SENSE_ILLEGAL_REQUEST, ASC_INVALID_COMMAND);
        break;
    }
    }

    /* Data the host sends with a refused command is read and dropped. */
    if (!data_in && s_msc_expected > 0) {
        s_msc_stage = MSC_STAGE_DATA_OUT;
        return usbd_edpt_xfer(rhport, s_msc_ep_out, s_msc_buf, sizeof(s_msc_buf), false);
    }
    return msc_finish(rhport, data_in);
}

static void msc_reset(uint8_t rhport)
{
    s_msc_ep_in = 0;
    s_msc_ep_out = 0;
    s_msc_stage = MSC_STAGE_CMD;
}

static uint16_t msc_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    TU_VERIFY(itf->bInterfaceClass == TUSB_CLASS_MSC, 0);
    uint16_t len = sizeof(tusb_desc_interface_t) + 2 * sizeof(tusb_desc_endpoint_t);
    TU_VERIFY(max_len >= len, 0);
    TU_ASSERT(usbd_open_edpt_pair(rhport, tu_desc_next(itf), 2, TUSB_XFER_BULK, &s_msc_ep_out, &s_msc_ep_in), 0);
    TU_ASSERT(msc_receive_cbw(rhport), 0);
    return len;
}

static bool msc_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    /* Recovery after a stall: the host clears it, then the pending status or next command follows. */
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD &&
        request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_ENDPOINT &&
        request->bRequest == TUSB_REQ_CLEAR_FEATURE && request->wValue == TUSB_REQ_FEATURE_EDPT_HALT) {
        uint8_t ep = tu_u16_low(request->wIndex);
        if (s_msc_stage == MSC_STAGE_NEED_RESET) {
            usbd_edpt_stall(rhport, ep);
        } else if (ep == s_msc_ep_in && s_msc_stage == MSC_STAGE_STATUS) {
            msc_send_csw(rhport);
        } else if (ep == s_msc_ep_out && s_msc_stage == MSC_STAGE_CMD && usbd_edpt_ready(rhport, s_msc_ep_out)) {
            msc_receive_cbw(rhport);
        }
        return true;
    }
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS) {
        return false;
    }
    static CFG_TUSB_MEM_ALIGN uint8_t max_lun;
    switch (request->bRequest) {
    case MSC_REQUEST_RESET:
        s_msc_stage = MSC_STAGE_CMD;
        if (usbd_edpt_ready(rhport, s_msc_ep_out)) {
            msc_receive_cbw(rhport);
        }
        return tud_control_status(rhport, request);
    case MSC_REQUEST_MAX_LUN:
        max_lun = 0;
        return tud_control_xfer(rhport, request, &max_lun, 1);
    }
    return false;
}

static bool msc_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    if (ep_addr == s_msc_ep_out) {
        if (s_msc_stage == MSC_STAGE_CMD) {
            return msc_command(rhport, xferred_bytes);
        }
        if (s_msc_stage == MSC_STAGE_DATA_OUT) {
            s_msc_done += xferred_bytes;
            if (s_msc_done < s_msc_expected && xferred_bytes == sizeof(s_msc_buf)) {
                return usbd_edpt_xfer(rhport, s_msc_ep_out, s_msc_buf, sizeof(s_msc_buf), false);
            }
            return msc_send_csw(rhport);
        }
        return true;
    }
    if (ep_addr == s_msc_ep_in) {
        if (s_msc_stage == MSC_STAGE_DATA_IN) {
            s_msc_done += xferred_bytes;
            return msc_read_next(rhport);
        }
        if (s_msc_stage == MSC_STAGE_STATUS) {
            return msc_receive_cbw(rhport);
        }
    }
    return true;
}

static const usbd_class_driver_t s_app_drivers[] = {
    {
        .name = "CCID",
        .init = driver_init,
        .deinit = driver_deinit,
        .reset = ccid_reset,
        .open = ccid_open,
        .control_xfer_cb = ccid_control_xfer_cb,
        .xfer_cb = ccid_xfer_cb,
    },
    {
        .name = "MSC",
        .init = driver_init,
        .deinit = driver_deinit,
        .reset = msc_reset,
        .open = msc_open,
        .control_xfer_cb = msc_control_xfer_cb,
        .xfer_cb = msc_xfer_cb,
    },
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = sizeof(s_app_drivers) / sizeof(s_app_drivers[0]);
    return s_app_drivers;
}

void usb_hid_ccid_set_callbacks(usb_hid_ccid_rx_cb_t rx, usb_hid_ccid_tx_done_cb_t tx_done, void *ctx)
{
    s_ccid_ctx = ctx;
    s_ccid_rx_cb = rx;
    s_ccid_tx_done_cb = tx_done;
}

esp_err_t usb_hid_ccid_send(const uint8_t *data, size_t len)
{
    if (!tud_mounted() || !s_ccid_ep_in || len > UINT16_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usbd_edpt_claim(0, s_ccid_ep_in)) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = usbd_edpt_xfer(0, s_ccid_ep_in, (uint8_t *)data, (uint16_t)len, false);
    if (!ok) {
        usbd_edpt_release(0, s_ccid_ep_in);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t usb_hid_ccid_notify(const uint8_t *data, size_t len)
{
    if (!tud_mounted() || !s_ccid_ep_int || len > sizeof(s_ccid_int_buf)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usbd_edpt_claim(0, s_ccid_ep_int)) {
        return ESP_ERR_NOT_FINISHED;
    }
    memcpy(s_ccid_int_buf, data, len);
    bool ok = usbd_edpt_xfer(0, s_ccid_ep_int, s_ccid_int_buf, (uint16_t)len, false);
    if (!ok) {
        usbd_edpt_release(0, s_ccid_ep_int);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t usb_hid_start(usb_hid_mount_cb_t cb, void *ctx)
{
    s_mount_cb = cb;
    s_mount_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_efuse_mac_get_default(mac), TAG, "mac");
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    memcpy(s_config_desc, s_config_card, sizeof(s_config_card));
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_event_cb);
    cfg.descriptor.device = &s_device_desc;
    cfg.descriptor.full_speed_config = s_config_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STRID_COUNT;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&cfg), TAG, "driver install");
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

/* Long enough for hosts to register the unplug before the device comes back changed. */
#define REENUMERATE_GAP_MS  300

esp_err_t usb_hid_set_disk(const usb_hid_disk_t *disk)
{
    ESP_RETURN_ON_FALSE(!disk || (disk->read && disk->block_count), ESP_ERR_INVALID_ARG, TAG, "disk");
    if (disk == s_disk) {
        return ESP_OK;
    }
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(REENUMERATE_GAP_MS));
    s_disk = disk;
    s_disk_ejected = false;
    if (disk) {
        memcpy(s_config_desc, s_config_disk, sizeof(s_config_disk));
        s_device_desc.idProduct = USB_PID_DISK;
    } else {
        memcpy(s_config_desc, s_config_card, sizeof(s_config_card));
        s_device_desc.idProduct = USB_PID;
    }
    tud_connect();
    ESP_LOGI(TAG, "%s", disk ? "drive in place of the smartcard" : "smartcard back");
    return ESP_OK;
}

bool usb_hid_disk_ejected(void)
{
    return s_disk_ejected;
}

bool usb_hid_is_mounted(void)
{
    return tud_mounted();
}

uint8_t usb_hid_keyboard_leds(void)
{
    return s_leds;
}

void usb_hid_fido_set_rx_cb(usb_hid_fido_rx_cb_t cb, void *ctx)
{
    s_fido_rx_ctx = ctx;
    s_fido_rx_cb = cb;
}

bool usb_hid_fido_ready(void)
{
    return tud_mounted() && tud_hid_n_ready(HID_INSTANCE_FIDO);
}

esp_err_t usb_hid_fido_send(const uint8_t report[USB_HID_FIDO_REPORT_SIZE])
{
    if (!usb_hid_fido_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return tud_hid_n_report(HID_INSTANCE_FIDO, 0, report, USB_HID_FIDO_REPORT_SIZE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wait_ready(void)
{
    for (int i = 0; i < HID_READY_TIMEOUT_MS; i++) {
        if (!tud_mounted()) {
            return ESP_ERR_INVALID_STATE;
        }
        if (tud_hid_n_ready(HID_INSTANCE_KEYBOARD)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t send_report(uint8_t report_id, const void *report, uint16_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = wait_ready();
    if (ret == ESP_OK && !tud_hid_n_report(HID_INSTANCE_KEYBOARD, report_id, report, len)) {
        ret = ESP_FAIL;
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t usb_hid_keyboard_report(uint8_t modifiers, const uint8_t keycodes[USB_HID_KEYBOARD_KEYS])
{
    hid_keyboard_report_t report = { .modifier = modifiers };
    if (keycodes) {
        memcpy(report.keycode, keycodes, USB_HID_KEYBOARD_KEYS);
    }
    return send_report(REPORT_ID_KEYBOARD, &report, sizeof(report));
}

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    const hid_mouse_report_t report = {
        .buttons = buttons,
        .x = x,
        .y = y,
        .wheel = wheel,
        .pan = pan,
    };
    return send_report(REPORT_ID_MOUSE, &report, sizeof(report));
}

esp_err_t usb_hid_consumer_report(uint16_t usage)
{
    return send_report(REPORT_ID_CONSUMER, &usage, sizeof(usage));
}
