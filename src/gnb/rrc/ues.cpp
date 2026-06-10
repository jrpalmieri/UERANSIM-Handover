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

RrcUeContext *GnbRrcTask::tryFindUeByCrnti(int crnti)
{
    for (const auto &entry : m_ueCtx)
    {
        auto *ctx = entry.second;
        if (!ctx)
            continue;

        if (ctx->cRnti == crnti)
            return ctx;
    }

    return nullptr;
}

RrcUeContext *GnbRrcTask::tryFindUeByUeId(int64_t ueId)
{
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || it->second == nullptr)
        return nullptr;

    auto *ctx = it->second;
    if (ctx->ueId != ueId)
    {
        m_logger->warn("UE[%ld]: tryFindUeByUeId - RRC context key mismatch: keyUeId=%d ctxUeId=%d cRnti=%d",
                       ueId, ueId, ctx->ueId, ctx->cRnti);
        return nullptr;
    }

    return ctx;

    return nullptr;
}

bool GnbRrcTask::getUeContext(int64_t ueId, std::optional<RrcUeContext> &out)
{
    auto *ctx = tryFindUeByUeId(ueId);
    if (ctx == nullptr)
        return false;
    out.emplace(*ctx);
    return true;
}


/**
 * @brief Deletes the UE's RRC context.
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
