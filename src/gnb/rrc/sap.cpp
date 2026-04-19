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
        m_logger->debug("UE[%d] new signal detected", msg.ueId);
        triggerSysInfoBroadcast();  // Send MIB and SIBs
        break;
    }
    case NmGnbRlsToRrc::UPLINK_RRC: {
        
        // Update the UE context with the latest position reported in heartbeats.
        if (msg.hasPosData)
        {
            auto *ue = tryFindUeByUeId(msg.ueId);
            if (ue)
            {
                ue->uePosition.altitude = msg.uePos.altitude;
                ue->uePosition.latitude = msg.uePos.latitude;
                ue->uePosition.longitude = msg.uePos.longitude;
            }
        }
        handleUplinkRrc(msg.ueId, msg.cRnti, msg.rrcChannel, msg.data);
        break;
    }
    }
}

} // namespace nr::gnb
