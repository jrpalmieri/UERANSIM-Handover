//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/nts.hpp>
#include <ue/rls/task.hpp>

namespace nr::ue
{

/**
 * @brief Routine that handles messages from RLS task to RRC layer.
 *  These messages are usually triggered by the events related to the radio link, 
 *  such as signal change, radio link failure, and downlink RRC delivery.
 * 
 * @param msg - the message
 */
void UeRrcTask::handleRlsSapMessage(NmUeRlsToRrc &msg)
{
    switch (msg.present)
    {
    // received for new cells and for signal strngth below RLF
    case NmUeRlsToRrc::SIGNAL_CHANGED: {
        handleCellSignalChange(msg.cellId, msg.dbm);
        break;
    }
    case NmUeRlsToRrc::DOWNLINK_RRC_DELIVERY: {
        handleDownlinkRrc(msg.cellId, msg.channel, msg.pdu);
        break;
    }
    // received for transmission errors such as duplicate PDU ID, PDU ID full
    case NmUeRlsToRrc::RADIO_LINK_FAILURE: {
        handleRadioLinkFailure(msg.rlfCause, msg.cellId, 0);
        break;
    }
    }
}

/**
 * @brief Routine that handles messages from NAS task to RRC layer.
 *  These messages are usually triggered by the events related to the NAS layer, 
 *  such as uplink NAS delivery, local release connection, and RRC notify.
 * 
 * @param msg - the message
 */
void UeRrcTask::handleNasSapMessage(NmUeNasToRrc &msg)
{
    switch (msg.present)
    {
    case NmUeNasToRrc::UPLINK_NAS_DELIVERY: {
        deliverUplinkNas(msg.pduId, std::move(msg.nasPdu));
        break;
    }
    case NmUeNasToRrc::LOCAL_RELEASE_CONNECTION: {
        // TODO: handle treat barred
        (void)msg.treatBarred;

        switchState(ERrcState::RRC_IDLE);
        m_base->rlsTask->push(std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::RESET_STI));
        m_base->nasTask->push(std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::RRC_CONNECTION_RELEASE));
        break;
    }
    case NmUeNasToRrc::RRC_NOTIFY: {
        triggerCycle();
        break;
    }
    case NmUeNasToRrc::PERFORM_UAC: {
        if (!msg.uacCtl->isExpiredForProducer())
            performUac(msg.uacCtl);
        break;
    }
    }
}

} // namespace nr::ue