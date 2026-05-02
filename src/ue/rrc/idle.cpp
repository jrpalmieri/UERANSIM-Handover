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
 * @brief Idle mode cell selection routine. This is triggered by the RRC cycle timer.
 * Requirements:
 * - RRC state is not RRC_CONNECTED
 * - We are at least 1 second after startup
 * - We are at least 4 seconds after startup and a PLMN has been selected.
 * - 
 * - Radio link failure
 * - Periodic cell selection retry if no suitable cell is found (every 2.5 seconds, starting immediately after startup)
 *  
 */
void UeRrcTask::performCellSelection()
{
    // sanity check - can't do cell selection in CONNECTED state
    if (m_state == ERrcState::RRC_CONNECTED)
        return;

    int64_t currentTime = utils::CurrentTimeMillis();

    m_logger->info("Starting idle mode cell selection, current time since startup: %dms, current cellId: %d",
                    static_cast<int>(currentTime - m_startedTime), m_base->shCtx.currentCell.get().cellId);

    //    if (currentTime - m_startedTime <= 1000LL && m_cellDesc.empty())
    // always wait 1sec after startup before doing cell selection, to allow
    //    cell data to get populated
    if (currentTime - m_startedTime <= 1000LL) 
    {
        m_logger->info("Too early for cell selection, waiting for 1 second after startup");
        return;
    }
    if (currentTime - m_startedTime <= 4000LL && !m_base->shCtx.selectedPlmn.get().hasValue())
    {
        m_logger->info("No PLMN selected and within 4 seconds of startup. Exiting");
        return;
    }
    // save the last cell info we were camped on, to compare after selection and log if changed
    auto lastCell = m_base->shCtx.currentCell.get();

    bool shouldLogErrors = lastCell.cellId != 0 || (currentTime - m_lastTimePlmnSearchFailureLogged >= 30'000LL);

    ActiveCellInfo cellInfo;
    CellSelectionReport report;

    bool cellFound = false;
    // if PLMN is selected, look for suitable cell first
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

    // if no suitable cell found (or outside of PLMN), look for acceptable cell (emergency services)
    if (!cellFound)
    {
        m_logger->info("No suitable cell found, looking for acceptable cell (for emergency services)");

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

    // if still no cell found, end cell selection
    if (!cellFound)
    {
        m_logger->info("No suitable or acceptable cell found, exiting.");
        return;
    }

    int selectedCell = cellInfo.cellId;

    // update shared RRC-NAS context with selected cellId
    m_base->shCtx.currentCell.set(cellInfo);

    if (selectedCell != 0) {

        if (selectedCell != lastCell.cellId)
        {
            m_logger->info("Selected new cell [%d]: plmn[%s] tac[%d] category[%s] dbm[%d]", cellInfo.cellId, 
                        ToJson(cellInfo.plmn).str().c_str(), cellInfo.tac,
                        ToJson(cellInfo.category).str().c_str(), cellInfo.dbm);

            auto w1 = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::ASSIGN_CURRENT_CELL);
            w1->cellId = selectedCell;
            m_base->rlsTask->push(std::move(w1));

            auto w2 = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
            w2->previousTai = Tai{lastCell.plmn, lastCell.tac};
            m_base->nasTask->push(std::move(w2));
        }
        else
        {
            m_logger->info("Selected the same cell [%d] as previous selection: plmn[%s] tac[%d] category[%s] dbm[%d]", 
                        cellInfo.cellId, ToJson(cellInfo.plmn).str().c_str(), cellInfo.tac,
                        ToJson(cellInfo.category).str().c_str(), cellInfo.dbm);
        }
    }
}

/**
 * @brief Cell selection routine for suitable cells. This looks for suitable cells in the m_cellDesc map.
 *   A suitable cell has the required SIB1 and MIB information, the correct PLMNID, is not barred or reserved, and
 *   has a tracking area code that is not on the forbidden list.
 *   If multiple suitable cells are found, the one with the strongest dbm is returned in cellInfo.
 * 
 * @param[out] cellInfo cell information of the selected cell (returned)
 * @param[out] report report on results of cell selection attempt (returned)
 * @return true - cell found
 */
bool UeRrcTask::lookForSuitableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report)
{
    Plmn selectedPlmn = m_base->shCtx.selectedPlmn.get();
    if (!selectedPlmn.hasValue())
        return false;

    // vector of cellIds that are suitable candidates for selection, 
    //   i.e. they have the required SIB1 and MIB information, correct PLMNID, not barred or reserved, 
    //   and not in forbidden TAI list    
    std::vector<std::pair<int, int>> candidates;

    for (auto &item : m_cellDesc)
    {
        auto &cell = item.second;
        m_logger->debug("Evaluating cellId %d for suitability, plmn[%s], tac[%d], mib[%s], sib1[%s]", item.first,
                         ToJson(cell.sib1.plmn).str().c_str(), cell.sib1.tac, cell.mib.hasMib ? "yes" : "no",
                         cell.sib1.hasSib1 ? "yes" : "no");

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

        // signal strength threshold check
        int dbm = MIN_RSRP;
        {
            // signal strength is from global cellDbMeas map
            std::shared_lock lock(m_base->cellDbMeasMutex);
            auto it = m_base->cellDbMeas.find(item.first);
            if (it != m_base->cellDbMeas.end())
                dbm = it->second;
        }
        m_logger->debug("CellId %d has signal strength %d dBm", item.first, dbm);

        if (dbm < cons::RLF_RSRP)
        {
            report.weakSignalCells++;
            continue;
        }

        // Meets criteria
        candidates.push_back(std::make_pair(item.first, dbm));
        m_logger->debug("CellId %d is a suitable candidate", item.first);
    }

    if (candidates.empty())
        return false;


    // sort the candidate list by signal strength (dbm) in descending order
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);

        std::sort(candidates.begin(), candidates.end(), [this](std::pair<int, int> a, std::pair<int, int> b) {
            return a.second > b.second;
        });
    }

    // select the top of the list as the best candidate
    int selectedId = candidates[0].first;
    auto &selectedCell = m_cellDesc[selectedId];

    cellInfo = {};
    cellInfo.cellId = selectedId;
    cellInfo.plmn = selectedCell.sib1.plmn;
    cellInfo.tac = selectedCell.sib1.tac;
    cellInfo.category = ECellCategory::SUITABLE_CELL;
    cellInfo.dbm = candidates[0].second;

    return true;
}


bool UeRrcTask::lookForAcceptableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report)
{
    std::vector<std::pair<int, int>> candidates;

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
        
        // signal strength threshold check
        int dbm = MIN_RSRP;
        {
            // signal strength is from global cellDbMeas map
            std::shared_lock lock(m_base->cellDbMeasMutex);
            auto it = m_base->cellDbMeas.find(item.first);
            if (it != m_base->cellDbMeas.end())
                dbm = it->second;
        }

        if (dbm < cons::RLF_RSRP)
        {
            report.weakSignalCells++;
            continue;
        }

        // It seems acceptable
        candidates.push_back(std::make_pair(item.first, dbm));
    }

    if (candidates.empty())
        return false;


    Plmn selectedPlmn = m_base->shCtx.selectedPlmn.get();

    // sort the candidate list by signal strength (dbm) in descending order
    // signal strength is from global celldbmeas map
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);

        std::sort(candidates.begin(), candidates.end(), [this](std::pair<int, int> a, std::pair<int, int> b) {
            return a.second > b.second;
        });
    }

    // Then order candidates by PLMN priority if we have a selected PLMN
    if (selectedPlmn.hasValue())
    {
        // Using stable-sort here
        std::stable_sort(candidates.begin(), candidates.end(), [this, &selectedPlmn](std::pair<int, int> a, std::pair<int, int> b) {
            auto &cellA = m_cellDesc[a.first];
            auto &cellB = m_cellDesc[b.first];

            bool matchesA = cellA.sib1.hasSib1 && cellA.sib1.plmn == selectedPlmn;
            bool matchesB = cellB.sib1.hasSib1 && cellB.sib1.plmn == selectedPlmn;

            return matchesB < matchesA;
        });
    }

    int selectedId = candidates[0].first;
    auto &selectedCell = m_cellDesc[selectedId];

    cellInfo = {};
    cellInfo.cellId = selectedId;
    cellInfo.plmn = selectedCell.sib1.plmn;
    cellInfo.tac = selectedCell.sib1.tac;
    cellInfo.category = ECellCategory::ACCEPTABLE_CELL;
    cellInfo.dbm = candidates[0].second;

    return true;
}

} // namespace nr::ue