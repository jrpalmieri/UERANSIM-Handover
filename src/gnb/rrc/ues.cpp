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

namespace nr::gnb
{

RrcUeContext *GnbRrcTask::createUe(int64_t ueId, int crnti)
{
    auto *ctx = new RrcUeContext(crnti);
    ctx->ueId = ueId;
    m_ueCtx[ueId] = ctx;
    return ctx;
}

// Thread-safe snapshot of a UE's RRC context, for callers on other task
// threads (e.g. Xn context transfer).  See m_ueCtxMutex in task.hpp.
bool GnbRrcTask::getUeContext(int64_t ueId, std::optional<RrcUeContext> &out)
{
    std::shared_lock<std::shared_mutex> lock(m_ueCtxMutex);

    auto *ctx = findCtxByUeId(ueId);
    if (ctx == nullptr)
        return false;
    out.emplace(*ctx);
    return true;
}


/**
 * @brief Deletes the UE's RRC context.
 * 
 * Performs C-RNTI release so it can be reused for future UEs.  Deletes the RRC context object and removes it from the m_ueCtx map.
 *
 * @param ueId
 */
void GnbRrcTask::ueContextRelease(int64_t ueId)
{
    auto *ctx = findCtxByUeId(ueId);
    if (ctx)
    {
        releaseCrnti(ctx->cRnti);
        delete ctx;
        m_ueCtx.erase(ueId);
        m_logger->info("UE[%ld]: ueContextRelease - RRC context released", ueId);
        return;
    }

    m_logger->warn("UE[%ld]: ueContextRelease - context not found", ueId);
}

} // namespace nr::gnb
