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
    if (m_ueCtx.count(ueId))
        return m_ueCtx[ueId];

    return nullptr;
}

} // namespace nr::gnb
