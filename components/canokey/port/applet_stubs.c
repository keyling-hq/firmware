/*
 * canokey-core's APDU router calls every applet it knows. PIV, OATH and admin aren't built: their
 * entry points answer as if the applet didn't exist (PIV is also disabled in the device config,
 * so it is never selected).
 */
#include <admin.h>
#include <apdu.h>
#include <oath.h>
#include <piv.h>

static int not_found(RAPDU *rapdu)
{
    rapdu->len = 0;
    rapdu->sw = SW_FILE_NOT_FOUND;
    return 0;
}

int piv_process_apdu(const CAPDU *capdu, RAPDU *rapdu)
{
    return not_found(rapdu);
}

int piv_process_apdu_message(RAPDU_CHAINING *rapdu_chaining, CAPDU *capdu, RAPDU *rapdu)
{
    return not_found(rapdu);
}

int oath_process_apdu(const CAPDU *capdu, RAPDU *rapdu)
{
    return not_found(rapdu);
}

int admin_process_apdu(const CAPDU *capdu, RAPDU *rapdu)
{
    return not_found(rapdu);
}
