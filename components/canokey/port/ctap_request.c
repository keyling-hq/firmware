/*
 * Notes the kind of each CTAP command and its relying party, and of each smartcard APDU, so the
 * presence prompt can say what is being approved. canokey-core doesn't pass this to
 * wait_for_user_presence(), so its entry points are wrapped at link time (-Wl,--wrap) and each
 * request is peeked at before it runs.
 */
#include <string.h>
#include "ctap_request.h"
#include "canokey_gate.h"
#include <cbor.h>
#include <ctap.h>
#include <ctap-internal.h>
#include <ctaphid.h>
#include <apdu.h>

/* Canonical CTAP CBOR sorts keys, so the relying party is near the start of the request. */
#define PEEK_BYTES          320
#define WINDOWS_PROBE_RP    "SelectDevice"
/* No CTAP command: canokey answers CTAP2_ERR_UNHANDLED_REQUEST. */
#define REFUSED_COMMAND     0x3F

/* OpenPGP card commands that use a private key. */
#define PGP_INS_PSO         0x2A
#define PGP_PSO_SIGN        0x9E9A
#define PGP_PSO_DECRYPT     0x8086
#define PGP_INS_AUTH        0x88

static uint8_t s_peek[PEEK_BYTES];
static volatile canokey_request_t s_kind;
static char s_rp_id[CANOKEY_RP_ID_MAX];

static void copy_rp_id(CborValue *value)
{
    size_t len = sizeof(s_rp_id);
    if (!cbor_value_is_text_string(value) || cbor_value_copy_text_string(value, s_rp_id, &len, NULL) != CborNoError) {
        s_rp_id[0] = '\0';
    }
}

/* makeCredential: key 2 is the rp map, whose "id" is the relying party. */
static void find_rp_in_map(CborValue *rp)
{
    CborValue field;
    if (!cbor_value_is_map(rp) || cbor_value_map_find_value(rp, "id", &field) != CborNoError) {
        return;
    }
    copy_rp_id(&field);
}

/* Finds a request parameter by its integer key; the value refers to the parser's data. */
static bool find_param(const uint8_t *params, size_t len, int wanted, CborParser *parser, CborValue *value)
{
    CborValue it;
    if (cbor_parser_init(params, len, 0, parser, &it) != CborNoError || !cbor_value_is_map(&it) ||
        cbor_value_enter_container(&it, value) != CborNoError) {
        return false;
    }
    while (!cbor_value_at_end(value)) {
        int key;
        if (!cbor_value_is_integer(value) || cbor_value_get_int(value, &key) != CborNoError ||
            cbor_value_advance(value) != CborNoError) {
            return false;
        }
        if (key == wanted) {
            return true;
        }
        if (cbor_value_advance(value) != CborNoError) {
            return false;
        }
    }
    return false;
}

static void parse_rp(const uint8_t *params, size_t len, int rp_key, bool rp_is_map)
{
    CborParser parser;
    CborValue value;
    if (!find_param(params, len, rp_key, &parser, &value)) {
        return;
    }
    if (rp_is_map) {
        find_rp_in_map(&value);
    } else {
        copy_rp_id(&value);
    }
}

/*
 * A makeCredential with an empty pinUvAuthParam only waits for a touch and then fails: hosts use
 * it to let the user pick a key, e.g. before Windows resets one.
 */
static bool is_touch_probe(const uint8_t *req, size_t len)
{
    CborParser parser;
    CborValue value;
    size_t param_len;
    return req[0] == CTAP_MAKE_CREDENTIAL && find_param(req + 1, len - 1, MC_REQ_PIN_UV_AUTH_PARAM, &parser, &value) &&
           cbor_value_is_byte_string(&value) && cbor_value_get_string_length(&value, &param_len) == CborNoError &&
           param_len == 0;
}

static void note_request(const uint8_t *req, size_t len)
{
    s_rp_id[0] = '\0';
    s_kind = CANOKEY_REQUEST_OTHER;
    if (len < 1) {
        return;
    }
    switch (req[0]) {
    case CTAP_MAKE_CREDENTIAL:
        parse_rp(req + 1, len - 1, MC_REQ_RP, true);
        s_kind = strcmp(s_rp_id, WINDOWS_PROBE_RP) == 0 ? CANOKEY_REQUEST_SELECT : CANOKEY_REQUEST_REGISTER;
        break;
    case CTAP_GET_ASSERTION:
        parse_rp(req + 1, len - 1, GA_REQ_RP_ID, false);
        s_kind = CANOKEY_REQUEST_SIGN_IN;
        break;
    case CTAP_SELECTION:
        s_kind = CANOKEY_REQUEST_SELECT;
        break;
    case CTAP_RESET:
        s_kind = CANOKEY_REQUEST_RESET;
        break;
    default:
        break;
    }
}

void ctap_request_current(canokey_request_t *kind, const char **rp_id)
{
    *kind = s_kind;
    *rp_id = s_rp_id;
}

int __real_ctap_process_cbor_stream_with_src(uint8_t *req, size_t req_len, uint8_t *scratch, size_t scratch_len,
                                             CTAPHID_TxSource *source, ctap_src_t src);
int __real_ctap_process_cbor_stream_source_with_src(const ctap_req_src_t *req_src, uint8_t *scratch,
                                                    size_t scratch_len, CTAPHID_TxSource *source, ctap_src_t src);

/* While the profile is locked, only what a security key reset needs is served. */
static bool refused(const uint8_t *req, size_t len)
{
    if (!canokey_port_locked() || len < 1 || req[0] == CTAP_GET_INFO || req[0] == CTAP_RESET ||
        req[0] == CTAP_SELECTION || req[0] == REFUSED_COMMAND || is_touch_probe(req, len)) {
        return false;
    }
    canokey_port_report_locked();
    return true;
}

static int refuse(uint8_t *scratch, size_t scratch_len, CTAPHID_TxSource *source, ctap_src_t src)
{
    uint8_t cmd = REFUSED_COMMAND;
    return __real_ctap_process_cbor_stream_with_src(&cmd, 1, scratch, scratch_len, source, src);
}

int __wrap_ctap_process_cbor_stream_with_src(uint8_t *req, size_t req_len, uint8_t *scratch, size_t scratch_len,
                                             CTAPHID_TxSource *source, ctap_src_t src)
{
    if (req) {
        note_request(req, req_len);
        if (refused(req, req_len)) {
            return refuse(scratch, scratch_len, source, src);
        }
    }
    return __real_ctap_process_cbor_stream_with_src(req, req_len, scratch, scratch_len, source, src);
}

int __wrap_ctap_process_cbor_stream_source_with_src(const ctap_req_src_t *req_src, uint8_t *scratch,
                                                    size_t scratch_len, CTAPHID_TxSource *source, ctap_src_t src)
{
    if (req_src && req_src->read) {
        size_t len = req_src->len < sizeof(s_peek) ? req_src->len : sizeof(s_peek);
        if (req_src->read(req_src->ctx, req_src->base_offset, s_peek, len) >= 0) {
            note_request(s_peek, len);
            if (refused(s_peek, len)) {
                return refuse(scratch, scratch_len, source, src);
            }
        }
    }
    return __real_ctap_process_cbor_stream_source_with_src(req_src, scratch, scratch_len, source, src);
}

int __real_ctap_process_apdu_with_src(const CAPDU *capdu, RAPDU *rapdu, ctap_src_t src);
int __real_ctap_process_apdu_source_with_src(const CAPDU *capdu, const ctap_req_src_t *req_src, RAPDU *rapdu,
                                             ctap_src_t src);

/* U2F: a locked profile answers as if waiting for a touch, so clients keep retrying until it opens. */
static bool u2f_refused(RAPDU *rapdu)
{
    if (!canokey_port_locked()) {
        return false;
    }
    canokey_port_report_locked();
    rapdu->len = 0;
    rapdu->sw = SW_CONDITIONS_NOT_SATISFIED;
    return true;
}

int __wrap_ctap_process_apdu_with_src(const CAPDU *capdu, RAPDU *rapdu, ctap_src_t src)
{
    return u2f_refused(rapdu) ? 0 : __real_ctap_process_apdu_with_src(capdu, rapdu, src);
}

int __wrap_ctap_process_apdu_source_with_src(const CAPDU *capdu, const ctap_req_src_t *req_src, RAPDU *rapdu,
                                             ctap_src_t src)
{
    return u2f_refused(rapdu) ? 0 : __real_ctap_process_apdu_source_with_src(capdu, req_src, rapdu, src);
}

void __real_process_apdu_from(CAPDU *capdu, RAPDU *rapdu, apdu_transport_t transport);

/* Only OpenPGP is reachable over CCID; FIDO over CCID keeps the generic prompt. */
void __wrap_process_apdu_from(CAPDU *capdu, RAPDU *rapdu, apdu_transport_t transport)
{
    s_rp_id[0] = '\0';
    s_kind = CANOKEY_REQUEST_OTHER;
    uint16_t p1p2 = (uint16_t)(capdu->p1 << 8 | capdu->p2);
    if (capdu->ins == PGP_INS_PSO && p1p2 == PGP_PSO_SIGN) {
        s_kind = CANOKEY_REQUEST_PGP_SIGN;
    } else if (capdu->ins == PGP_INS_PSO && p1p2 == PGP_PSO_DECRYPT) {
        s_kind = CANOKEY_REQUEST_PGP_DECRYPT;
    } else if (capdu->ins == PGP_INS_AUTH) {
        s_kind = CANOKEY_REQUEST_PGP_AUTH;
    }
    __real_process_apdu_from(capdu, rapdu, transport);
}
