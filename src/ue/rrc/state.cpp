//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <lib/asn/rrc.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/types.hpp>

namespace nr::ue
{

/**
 * @brief Pushes TRIGGER_CYCLE message onto msg queue to perform an RRC cycle.
 * 
 */
void UeRrcTask::triggerCycle()
{
    push(std::make_unique<NmUeRrcToRrc>(NmUeRrcToRrc::TRIGGER_CYCLE));
}

/**
 * @brief Performs an RRC cycle, which includes:
 * - Evaluating measurement events and sending reports if needed
 * - Evaluating CHO candidates and triggering CHO if needed
 * - Performing cell selection if in IDLE or INACTIVE state
 * 
 * This is called periodically by a timer, and also triggered by certain events such as cell signal change.
 */
void UeRrcTask::performCycle()
{
    if (m_state == ERrcState::RRC_CONNECTED)
    {
        // if the handover measurement framework is disabled, no need to run the measurement evaluations,
        //   so skip
        //if (!m_base->config->useHandoverMeasFramework) return;

        // Phase 2 priority: evaluate CHO first. If CHO triggers in this cycle,
        // suppress Rel-15 MeasurementReport generation for the same cycle.
        bool choTriggered = false;
        if (!m_handoverInProgress && !m_measurementEvalSuspended)
            choTriggered = evaluateChoCandidates();

        if (choTriggered)
            return;

        evaluateMeasurements();
    }
    else if (m_state == ERrcState::RRC_IDLE)
    {
        performCellSelection();
    }
    else if (m_state == ERrcState::RRC_INACTIVE)
    {
        performCellSelection();
    }
}

/**
 * @brief Switches the RRC state of the UE and performs necessary actions on state transition, 
 *  such as logging and notifying node listener.
 * 
 * @param state new RRC state to switch to
 */
void UeRrcTask::switchState(ERrcState state)
{
    ERrcState oldState = m_state;
    m_state = state;

    m_logger->info("UE switches to state [%s]", ToJson(state).str().c_str());

    if (m_base->nodeListener)
    {
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::RRC,
                                       ToJson(oldState).str(), ToJson(state).str());
    }

    onSwitchState(oldState, state);
}

void UeRrcTask::onSwitchState(ERrcState oldState, ERrcState newState)
{
}

} // namespace nr::ue