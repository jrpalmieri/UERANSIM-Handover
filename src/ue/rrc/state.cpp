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


static int getServingCellRsrp(int64_t servingCellId, const std::vector<std::pair<int64_t, int> >&allMeas)
{
    auto it = std::find_if(allMeas.begin(), allMeas.end(), [servingCellId](const auto &m) { return m.first == servingCellId; });
    if (it != allMeas.end())
        return it->second;

    return cons::MIN_RSRP; // if no measurement for serving cell, use minimum RSRP as default
}


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
 * This is called periodically by a timer, and also triggered by certain events.
 */
void UeRrcTask::performCycle()
{
    if (m_state == ERrcState::RRC_CONNECTED)
    {

        int64_t servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
        auto allMeas = m_base->cellDbMeas.getMeasurements(); // get the current measurements to evaluate against
        // get the serving cell id and RSRP for easier reference in loops below
        int servingCellRsrp = getServingCellRsrp(servingCellId, allMeas);

        // evaluate all the MeasIds and set their current state
        evaluateMeasurements(servingCellId, servingCellRsrp, allMeas);

        // evaluate CHO first. If CHO triggers in this cycle,
        // suppress Rel-15 MeasurementReport generation for the same cycle.
        bool choTriggered = false;
        if (!m_handoverInProgress && !m_measurementEvalSuspended)
            choTriggered = evaluateChoCandidates(servingCellId, allMeas);

        // CHO was triggered, don;t send measurement report for this cycle as per 38.331
        if (choTriggered)
            return;

        measurementReporting(servingCellId, servingCellRsrp);
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