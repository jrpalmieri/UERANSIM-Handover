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

namespace nr::ue
{

void UeRrcTask::handleCellSignalChange(int cellId, int dbm)
{
    
    // no longer participating = below min RSRP
    bool considerLost = dbm < cons::MIN_RSRP;
    // rlf = below RLF threshold but above min RSRP (i.e. still participating but very weak signal)
    bool radioLinkFailure = dbm < cons::RLF_RSRP && dbm >= cons::MIN_RSRP;

    // if not in the cell info list and not lost -> add it
    //   (includes cells that qualify as below RLF)
    if (!m_cellDesc.count(cellId))
    {
        if (!considerLost)
            notifyCellDetected(cellId, dbm);
    }
    // if in the cell info list, remove if lost
    else 
    {
        if (considerLost)
            notifyCellLost(cellId, dbm);

        if (radioLinkFailure)
            handleRadioLinkFailure(rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL, cellId, dbm);
    }

    // {
    //     if (considerLost)
    //         notifyCellLost(cellId);
    //     else
    //         m_cellDesc[cellId].dbm = dbm;
    // }
}

void UeRrcTask::notifyCellDetected(int cellId, int dbm)
{
    m_cellDesc[cellId] = {};
    // we don;t care about dbm in this struct, it is tracked in the separate cellDbMeas map
    m_cellDesc[cellId].dbm = dbm;

    m_logger->debug("New signal detected for cell[%d], total [%d] cells in coverage", cellId,
                    static_cast<int>(m_cellDesc.size()));

    // update the PLMN list
    updateAvailablePlmns();
}

void UeRrcTask::notifyCellLost(int cellId, int dbm)
{
    if (!m_cellDesc.count(cellId))
        return;


    // remove cell infomation
    m_cellDesc.erase(cellId);

    m_logger->debug("RLS connection lost for cell[%d], total [%d] cells in coverage", cellId,
                    static_cast<int>(m_cellDesc.size()));

    // update the PLMN list
    updateAvailablePlmns();

    // declare radio link failure
    handleRadioLinkFailure(rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL, cellId, dbm);
}

bool UeRrcTask::hasSignalToCell(int cellId)
{
    return m_cellDesc.count(cellId);
}

bool UeRrcTask::isActiveCell(int cellId)
{
    return m_base->shCtx.currentCell.get<int>([](auto &value) { return value.cellId; }) == cellId;
}

// scans the cell info list looking for SIB1 information to populate the available PLMN list
// in the shared context, then notifies NAS to update
void UeRrcTask::updateAvailablePlmns()
{
    m_base->shCtx.availablePlmns.mutate([this](std::unordered_set<Plmn> &value) {
        value.clear();
        for (auto &cellDesc : m_cellDesc)
            if (cellDesc.second.sib1.hasSib1)
                value.insert(cellDesc.second.sib1.plmn);
    });

    m_base->nasTask->push(std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::NAS_NOTIFY));
}

// void UeRrcTask::declareRadioLinkFailure(rls::ERlfCause cause)
// {
//     handleRadioLinkFailure(cause);
// }

void UeRrcTask::handleRadioLinkFailure(rls::ERlfCause cause, int cellId, int dbm)
{

    // check if this is the active cell
    bool isActiveCell = false;
    ActiveCellInfo lastActiveCell;
    m_base->shCtx.currentCell.mutate([&isActiveCell, &lastActiveCell, cellId](auto &value) {
        if (value.cellId == cellId)
        {
            lastActiveCell = value;
            value = {};
            isActiveCell = true;
        }
    });


    if (isActiveCell)
    {

        if (cause == rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL)
        {
            // in RRC_CONNECTED, losing the active cell means radio link failure
            if (m_state != ERrcState::RRC_IDLE) {

                m_logger->info("Radio Link failure detected for active cell[%d], dbm=%d", cellId, dbm);

                cancelAllChoCandidates();
                m_state = ERrcState::RRC_IDLE;

                // notify NAS
                m_base->nasTask->push(std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::RADIO_LINK_FAILURE));

            }

            else
            {
                // in RRC_IDLE, notify NAS
                m_logger->info("RRC_IDLE mode radio link failure detected for active cell[%d], dbm=%d", cellId, dbm);
                auto w = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
                w->previousTai = Tai{lastActiveCell.plmn, lastActiveCell.tac};
                m_base->nasTask->push(std::move(w));
            }
        }
        else
        {
            m_logger->info("Radio Link failure detected for active cell[%d], cause=%s.  Ignoring.", 
                cellId, std::to_string(static_cast<int>(cause)).c_str());

        }
    }

}

} // namespace nr::ue
