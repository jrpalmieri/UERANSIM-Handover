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

// RrcUeContext *GnbRrcTask::createUe(int crnti, int ueId)
// {
//     auto *ctx = new RrcUeContext(crnti);
//     ctx->ueId = ueId;
//     m_ueCtx[crnti] = ctx;
//     return ctx;
// }
RrcUeContext *GnbRrcTask::createUe(int ueId, int crnti)
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

RrcUeContext *GnbRrcTask::tryFindUeByUeId(int ueId)
{
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || it->second == nullptr)
        return nullptr;

    auto *ctx = it->second;
    if (ctx->ueId != ueId)
    {
        m_logger->warn("UE[%d] RRC context key mismatch: keyUeId=%d ctxUeId=%d cRnti=%d",
                       ueId, ueId, ctx->ueId, ctx->cRnti);
        return nullptr;
    }

    return ctx;

    return nullptr;
}

} // namespace nr::gnb
