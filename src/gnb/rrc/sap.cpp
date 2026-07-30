//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/ngap/task.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>
#include <utils/common_types.hpp>

namespace nr::gnb
{

void GnbRrcTask::handleRlsSapMessage(NmGnbRlsToRrc &msg)
{
    switch (msg.present)
    {
    case NmGnbRlsToRrc::SIGNAL_DETECTED: {
        m_logger->debug("UE[%ld] new signal detected", msg.ueId);
        triggerSysInfoBroadcast();  // Send MIB and SIBs
        break;
    }
    case NmGnbRlsToRrc::UPLINK_RRC: {
        
        handleUplinkRrc(msg.ueId, msg.cRnti, msg.rrcChannel, msg.data);
        break;
    }
    case NmGnbRlsToRrc::RADIO_LINK_FAILURE: {
        m_logger->info("UE[%ld] radio link failure received from RLS", msg.ueId);
        handleRadioLinkFailure(msg.ueId);
        break;
    }
    default: {
        m_logger->unhandledNts(msg);
        break;
    }
    }
}

} // namespace nr::gnb
