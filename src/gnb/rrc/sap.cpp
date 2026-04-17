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
                EcefPosition ecef = GeoToEcef(msg.uePos);
                ue->uePosition.isValid = true;
                ue->uePosition.epochMs = utils::CurrentTimeMillis();
                ue->uePosition.x       = ecef.x;
                ue->uePosition.y       = ecef.y;
                ue->uePosition.z       = ecef.z;
                // velocity is unknown from heartbeat position alone
                ue->uePosition.vx = 0.0;
                ue->uePosition.vy = 0.0;
                ue->uePosition.vz = 0.0;
            }
        }
        handleUplinkRrc(msg.ueId, msg.cRnti, msg.rrcChannel, msg.data);
        break;
    }
    }
}

} // namespace nr::gnb
