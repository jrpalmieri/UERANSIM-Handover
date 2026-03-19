//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <utils/random.hpp>

namespace nr::gnb
{

int GnbRrcTask::getNextTid()
{
    m_tidCounter++;
    m_tidCounter %= 4;
    return m_tidCounter;
}

/**
 * @brief Used to allocate a unique Cell Radio Network Temporary Identifier (C-RNTI) 
 * for a new UE connection.  The C-RNTI is used as the RRC UE context index and is 
 * included in the RRC messages sent to the UE.
 * 
 * @return int 
 */
int GnbRrcTask::allocateCrnti() const
{
    constexpr int kMinCrnti = 1;
    constexpr int kMaxCrnti = 0xFFFF;

    auto hasCrnti = [&](int crnti) {
        for (const auto &entry : m_ueCtx)
        {
            auto *ctx = entry.second;
            if (!ctx)
                continue;

            if (ctx->cRnti == crnti)
                return true;
        }
        return false;
    };

    Random rng;
    for (int attempt = 0; attempt < 256; ++attempt)
    {
        int candidate = rng.nextI(kMinCrnti, kMaxCrnti + 1);

        if (!hasCrnti(candidate))
            return candidate;
    }

    int start = rng.nextI(kMinCrnti, kMaxCrnti + 1);
    for (int offset = 0; offset <= (kMaxCrnti - kMinCrnti); ++offset)
    {
        int candidate = start + offset;
        if (candidate > kMaxCrnti)
            candidate = kMinCrnti + (candidate - kMaxCrnti - 1);

        if (!hasCrnti(candidate))
            return candidate;
    }

    return 0;
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

RrcUeContext* GnbRrcTask::findCtxByUeId(int ueId)
{
    if (ueId <= 0)
        return nullptr;

    if (m_ueCtx.count(ueId))
        return m_ueCtx[ueId];

    return nullptr;

}

} // namespace nr::gnb
