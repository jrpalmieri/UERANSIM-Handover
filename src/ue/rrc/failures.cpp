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

namespace nr::ue
{

    // moved to cells.cpp since it is more related to cell management
// void UeRrcTask::declareRadioLinkFailure(rls::ERlfCause cause)
// {
//     handleRadioLinkFailure(cause);
// }

// void UeRrcTask::handleRadioLinkFailure(rls::ERlfCause cause)
// {

//     cancelAllChoCandidates();
//     m_state = ERrcState::RRC_IDLE;

//     m_logger->info("Radio Link Failure detected, cause: {%s}", rls::RlfCauseToString(cause).c_str());
//     m_logger->info("Radio link state changed to RRC_IDLE");

//     m_base->nasTask->push(std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::RADIO_LINK_FAILURE));
//}

} // namespace nr::ue