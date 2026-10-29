/* Included by the generated ccid.c: while the host has been told the card is out, the slot is empty. */
#include "canokey_gate.h"

static uint8_t ccid_card_absent(uint8_t *response_type) {
  switch (bulkout_data.bMessageType) {
  case PC_TO_RDR_GETSLOTSTATUS:
  case PC_TO_RDR_ICCPOWEROFF:
    CCID_UpdateCommandStatus(BM_COMMAND_STATUS_NO_ERROR, BM_ICC_NO_ICC_PRESENT);
    return SLOT_NO_ERROR;
  case PC_TO_RDR_ICCPOWERON:
  case PC_TO_RDR_XFRBLOCK:
    *response_type = RDR_TO_PC_DATABLOCK;
    break;
  case PC_TO_RDR_GETPARAMETERS:
    *response_type = RDR_TO_PC_PARAMETERS;
    break;
  default:
    break;
  }
  CCID_UpdateCommandStatus(BM_COMMAND_STATUS_FAILED, BM_ICC_NO_ICC_PRESENT);
  return SLOTERROR_ICC_MUTE;
}

/* The card is back: the host powers it on again. */
static void ccid_card_present(void) {
  if (CCID_CardStatus() == BM_ICC_NO_ICC_PRESENT) {
    CCID_UpdateCommandStatus(BM_COMMAND_STATUS_NO_ERROR, BM_ICC_PRESENT_INACTIVE);
  }
}
