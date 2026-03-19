//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <algorithm>

#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/constants.hpp>

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const int MAX_RSRP = cons::MAX_RSRP; // maximum RSRP value (in dBm) to use when no measurement is available

namespace nr::ue
{

/**
 * @brief Idle mode cell selection routine. This is triggered in the following cases:
 * - UE startup (after 1 second, or after 4 seconds if no PLMN is selected yet)
 * - RRC connection release
 * - Radio link failure
 * - Periodic cell selection retry if no suitable cell is found (every 2.5 seconds, starting immediately after startup)
 *  
 */
void UeRrcTask::performCellSelection()
{
    if (m_state == ERrcState::RRC_CONNECTED)
        return;

    int64_t currentTime = utils::CurrentTimeMillis();

    if (currentTime - m_startedTime <= 1000LL && m_cellDesc.empty())
        return;
    if (currentTime - m_startedTime <= 4000LL && !m_base->shCtx.selectedPlmn.get().hasValue())
        return;

    auto lastCell = m_base->shCtx.currentCell.get();

    bool shouldLogErrors = lastCell.cellId != 0 || (currentTime - m_lastTimePlmnSearchFailureLogged >= 30'000LL);

    ActiveCellInfo cellInfo;
    CellSelectionReport report;

    bool cellFound = false;
    if (m_base->shCtx.selectedPlmn.get().hasValue())
    {
        cellFound = lookForSuitableCell(cellInfo, report);
        if (!cellFound)
        {
            if (shouldLogErrors)
            {
                if (!m_cellDesc.empty())
                {
                    m_logger->warn(
                        "Suitable cell selection failed in [%d] cells. [%d] out of PLMN, [%d] no SI, [%d] reserved, "
                        "[%d] barred, ftai [%d]",
                        static_cast<int>(m_cellDesc.size()), report.outOfPlmnCells, report.siMissingCells,
                        report.reservedCells, report.barredCells, report.forbiddenTaiCells);
                }
                else
                {
                    m_logger->warn("Suitable cell selection failed, no cell is in coverage");
                }

                m_lastTimePlmnSearchFailureLogged = currentTime;
            }
        }
    }

    if (!cellFound)
    {
        report = {};

        cellFound = lookForAcceptableCell(cellInfo, report);

        if (!cellFound)
        {
            if (shouldLogErrors)
            {
                if (!m_cellDesc.empty())
                {
                    m_logger->warn("Acceptable cell selection failed in [%d] cells. [%d] no SI, [%d] reserved, [%d] "
                                   "barred, ftai [%d]",
                                   static_cast<int>(m_cellDesc.size()), report.siMissingCells, report.reservedCells,
                                   report.barredCells, report.forbiddenTaiCells);
                }
                else
                {
                    m_logger->warn("Acceptable cell selection failed, no cell is in coverage");
                }

                m_logger->err("Cell selection failure, no suitable or acceptable cell found");

                m_lastTimePlmnSearchFailureLogged = currentTime;
            }
        }
    }

    int selectedCell = cellInfo.cellId;
    m_base->shCtx.currentCell.set(cellInfo);

    if (selectedCell != 0 && selectedCell != lastCell.cellId)
        m_logger->info("Selected cell plmn[%s] tac[%d] category[%s]", ToJson(cellInfo.plmn).str().c_str(), cellInfo.tac,
                       ToJson(cellInfo.category).str().c_str());

    // if a new cell has been selected, notify RLS and NAS tasks
    if (selectedCell != lastCell.cellId)
    {
        auto w1 = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::ASSIGN_CURRENT_CELL);
        w1->cellId = selectedCell;
        m_base->rlsTask->push(std::move(w1));

        auto w2 = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
        w2->previousTai = Tai{lastCell.plmn, lastCell.tac};
        m_base->nasTask->push(std::move(w2));
    }
}

/**
 * @brief Cell selection routine for suitable cells. This looks for suitable cells in the m_cellDesc map.
 *   A suitable cell has the required SIB1 and MIB information, the correct PLMNID, is not barred or reserved, and
 *   has a tracking area code that is not on the forbidden list.
 *   If multiple suitable cells are found, the one with the strongest dbm is returned in cellInfo.
 * 
 * @param cellInfo cell information of thr selected cell (returned)
 * @param report report on results of cell selection attempt (returned)
 * @return true - cell found
 * @return false - cell not found
 */
bool UeRrcTask::lookForSuitableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report)
{
    Plmn selectedPlmn = m_base->shCtx.selectedPlmn.get();
    if (!selectedPlmn.hasValue())
        return false;

    // vector of cellIds that are suitable candidates for selection, 
    //   i.e. they have the required SIB1 and MIB information, correct PLMNID, not barred or reserved, 
    //   and not in forbidden TAI list    
    std::vector<int> candidates;

    for (auto &item : m_cellDesc)
    {
        auto &cell = item.second;

        if (!cell.sib1.hasSib1)
        {
            report.siMissingCells++;
            continue;
        }

        if (!cell.mib.hasMib)
        {
            report.siMissingCells++;
            continue;
        }

        if (cell.sib1.plmn != selectedPlmn)
        {
            report.outOfPlmnCells++;
            continue;
        }

        if (cell.mib.isBarred)
        {
            report.barredCells++;
            continue;
        }

        if (cell.sib1.isReserved)
        {
            report.reservedCells++;
            continue;
        }

        Tai tai{cell.sib1.plmn, cell.sib1.tac};

        if (m_base->shCtx.forbiddenTaiRoaming.get<bool>([&tai](auto &item) {
                return std::any_of(item.begin(), item.end(), [&tai](auto &element) { return element == tai; });
            }))
        {
            report.forbiddenTaiCells++;
            continue;
        }

        if (m_base->shCtx.forbiddenTaiRps.get<bool>([&tai](auto &item) {
                return std::any_of(item.begin(), item.end(), [&tai](auto &element) { return element == tai; });
            }))
        {
            report.forbiddenTaiCells++;
            continue;
        }

        // It seems suitable
        candidates.push_back(item.first);
    }

    if (candidates.empty())
        return false;


    // sort the candidate list by signal strength (dbm) in descending order
    // signal strength is from global celldbmeas map
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);

        std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
            auto &cellA = m_base->cellDbMeas[a];
            auto &cellB = m_base->cellDbMeas[b];
            return cellB < cellA;
        });
    }

    int selectedId = candidates[0];
    auto &selectedCell = m_cellDesc[selectedId];

    cellInfo = {};
    cellInfo.cellId = selectedId;
    cellInfo.plmn = selectedCell.sib1.plmn;
    cellInfo.tac = selectedCell.sib1.tac;
    cellInfo.category = ECellCategory::SUITABLE_CELL;

    return true;
}

bool UeRrcTask::lookForAcceptableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report)
{
    std::vector<int> candidates;

    for (auto &item : m_cellDesc)
    {
        auto &cell = item.second;

        if (!cell.sib1.hasSib1)
        {
            report.siMissingCells++;
            continue;
        }

        if (!cell.mib.hasMib)
        {
            report.siMissingCells++;
            continue;
        }

        if (cell.mib.isBarred)
        {
            report.barredCells++;
            continue;
        }

        if (cell.sib1.isReserved)
        {
            report.reservedCells++;
            continue;
        }

        Tai tai{cell.sib1.plmn, cell.sib1.tac};

        if (m_base->shCtx.forbiddenTaiRoaming.get<bool>([&tai](auto &item) {
                return std::any_of(item.begin(), item.end(), [&tai](auto &element) { return element == tai; });
            }))
        {
            report.forbiddenTaiCells++;
            continue;
        }

        if (m_base->shCtx.forbiddenTaiRps.get<bool>([&tai](auto &item) {
                return std::any_of(item.begin(), item.end(), [&tai](auto &element) { return element == tai; });
            }))
        {
            report.forbiddenTaiCells++;
            continue;
        }

        // It seems acceptable
        candidates.push_back(item.first);
    }

    if (candidates.empty())
        return false;


    Plmn selectedPlmn = m_base->shCtx.selectedPlmn.get();

    // sort the candidate list by signal strength (dbm) in descending order
    // signal strength is from global celldbmeas map
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);

        std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
            auto &cellA = m_base->cellDbMeas[a];
            auto &cellB = m_base->cellDbMeas[b];
            return cellB < cellA;
        });
    }

    // Then order candidates by PLMN priority if we have a selected PLMN
    if (selectedPlmn.hasValue())
    {
        // Using stable-sort here
        std::stable_sort(candidates.begin(), candidates.end(), [this, &selectedPlmn](int a, int b) {
            auto &cellA = m_cellDesc[a];
            auto &cellB = m_cellDesc[b];

            bool matchesA = cellA.sib1.hasSib1 && cellA.sib1.plmn == selectedPlmn;
            bool matchesB = cellB.sib1.hasSib1 && cellB.sib1.plmn == selectedPlmn;

            return matchesB < matchesA;
        });
    }

    int selectedId = candidates[0];
    auto &selectedCell = m_cellDesc[selectedId];

    cellInfo = {};
    cellInfo.cellId = selectedId;
    cellInfo.plmn = selectedCell.sib1.plmn;
    cellInfo.tac = selectedCell.sib1.tac;
    cellInfo.category = ECellCategory::ACCEPTABLE_CELL;

    return true;
}

} // namespace nr::ue