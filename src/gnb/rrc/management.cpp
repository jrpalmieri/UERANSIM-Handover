//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

namespace nr::gnb
{

int GnbRrcTask::allocateCrnti()
{
    return m_crntiMgr.allocate();
}

void GnbRrcTask::releaseCrnti(int crnti)
{
    m_crntiMgr.release(crnti);
}

RrcUeContext* GnbRrcTask::findCtxByCrnti(int cRnti)
{
    if (cRnti <= 0)
        return nullptr;

    for (const auto &entry : m_ueCtx)
    {
        auto *ctx = entry.second;
        if (!ctx)
            continue;

        if (ctx->cRnti == cRnti)
            return ctx;
    }

    return nullptr;

}

RrcUeContext* GnbRrcTask::findCtxByUeId(int64_t ueId)
{
    if (ueId <= 0)
        return nullptr;

    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || it->second == nullptr)
        return nullptr;

    auto *ctx = it->second;
    if (ctx->ueId != ueId)
    {
        m_logger->warn("UE[%ld] RRC context key mismatch: keyUeId=%ld ctxUeId=%ld cRnti=%d",
                       ueId, ueId, ctx->ueId, ctx->cRnti);
        return nullptr;
    }

    return ctx;

    return nullptr;

}

} // namespace nr::gnb
