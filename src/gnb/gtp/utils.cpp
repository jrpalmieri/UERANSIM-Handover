//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "utils.hpp"

#include <utility>

#include <utils/common.hpp>

namespace nr::gnb
{

PduSessionTree::PduSessionTree() : mapByDownTeid{}, mapByUeId{}
{
}

/**
 * @brief Inserts the given session into the tree, indexed by UE ID, PDU session ID and
 * downlink TEID. The tree takes ownership of the session, which is moved into place.
 * On the duplicate TEID and duplicate PDU session paths nothing is inserted and the
 * caller's session is left untouched.
 *
 * @param ueId the identity of the UE that owns the session
 * @param psi the PDU session identity
 * @param session the session resource to store, moved from on a successful insert
 */
void PduSessionTree::insertSession(int64_t ueId, int psi, PduSessionResource &&session)
{
    UeSessionId usi = {ueId, psi};

    if (mapByDownTeid.count(session.downTunnel.teid))
        return;

    auto it = mapByUeId.find(ueId);
    if (it != mapByUeId.end())
    {
        for (const auto &item : it->second)
        {
            if (item.psi == psi)
                return;
        }
    }

    mapByDownTeid[session.downTunnel.teid] = usi;
    mapByUeId[ueId].emplace_back(std::move(session));
}

// returns the Ue Id and PDU session Id for a given downlink TEID (or null if not found)
UeSessionId* PduSessionTree::findByDownTeid(uint32_t teid)
{
    if (mapByDownTeid.count(teid))
        return &mapByDownTeid[teid];
    return nullptr;
}


// removes a specific session for a given UE ID and PDU session ID
void PduSessionTree::removeSession(int64_t ueId, int psi)
{

    if (mapByUeId.count(ueId))
    {
        auto &vector = mapByUeId[ueId];
        if (vector.size() > 0)
        {
            for (size_t i = 0; i < vector.size(); ++i)
            {
                if (vector[i].psi == psi)
                {
                    mapByDownTeid.erase(vector[i].downTunnel.teid);
                    vector.erase(vector.begin() + i);
                    break;
                }
            }
        }
        if (vector.empty())
            mapByUeId.erase(ueId);
    }


}

// removes all sessions for a given UE ID
void PduSessionTree::removeAllSessions(int64_t ueId)
{
    if (mapByUeId.count(ueId))
    {
        auto &vector = mapByUeId[ueId];
        for (auto &item : vector)
        {
            mapByDownTeid.erase(item.downTunnel.teid);
        }
        mapByUeId.erase(ueId);
    }
}

// for a given UE ID, returns the list of sessionid to TEID mappings for that UE
void PduSessionTree::enumerateByUe(int64_t ue, std::vector<PduSessionResource> &output)
{
    if (mapByUeId.count(ue) == 0)
        return;
    auto &vector = mapByUeId[ue];

    for (auto &item : vector)
        output.emplace_back(item);
}

// get a PDU session resource record for a given UE ID and PDU session ID
// return values:
// 0: success
// 1: UE context not found
// 2: No sessions stored for this UE
// 3: PDU session not found for this UE and PSI
int PduSessionTree::getSession(int64_t ueId, int psi, PduSessionResource *&session)
{

    if (!mapByUeId.count(ueId))
        return 1;

    auto &vector = mapByUeId[ueId];
    if (vector.size() == 0)
        return 2;

    for (auto &item : vector)    {
        if (item.psi == psi)
        {
            session = &item;
            return 0;
        }
    }
    return 3;
}


TokenBucket::TokenBucket(int64_t byteCapacity) : byteCapacity(byteCapacity)
{
    if (byteCapacity > 0)
    {
        this->refillTokensPerOneMillis = (double)byteCapacity / (double)REFILL_PERIOD;
        this->availableTokens = static_cast<double>(byteCapacity);
        this->lastRefillTimestamp = utils::CurrentTimeMillis();
    }
}

bool TokenBucket::tryConsume(uint64_t numberOfTokens)
{
    if (byteCapacity > 0)
    {
        refill();
        if (availableTokens < static_cast<double>(numberOfTokens))
            return false;
        else
        {
            availableTokens -= static_cast<double>(numberOfTokens);
            return true;
        }
    }
    else
        return true;
}

void TokenBucket::updateCapacity(uint64_t newByteCapacity)
{
    byteCapacity = newByteCapacity;
    if (newByteCapacity > 0)
        refillTokensPerOneMillis = (double)byteCapacity / (double)REFILL_PERIOD;
}

void TokenBucket::refill()
{
    int64_t currentTimeMillis = utils::CurrentTimeMillis();
    if (currentTimeMillis > lastRefillTimestamp)
    {
        int64_t millisSinceLastRefill = currentTimeMillis - lastRefillTimestamp;
        double refill = static_cast<double>(millisSinceLastRefill) * refillTokensPerOneMillis;
        availableTokens = std::min(static_cast<double>(byteCapacity), availableTokens + refill);
        lastRefillTimestamp = currentTimeMillis;
    }
}

bool RateLimiter::allowDownlinkPacket(int64_t ueId, int psi, uint64_t packetSize)
{

    if (downlinkByUe.count(ueId))
    {
        auto &bucket = downlinkByUe[ueId];
        if (!bucket->tryConsume(packetSize))
            return false;
    }

    UeSessionId ueSessionId{ueId, psi};
    if (downlinkBySession.count(ueSessionId))
    {
        auto &bucket = downlinkBySession[ueSessionId];
        if (!bucket->tryConsume(packetSize))
            return false;
    }

    return true;
}

bool RateLimiter::allowUplinkPacket(int64_t ueId, int psi, uint64_t packetSize)
{
    if (uplinkByUe.count(ueId))
    {
        auto &bucket = uplinkByUe[ueId];
        if (!bucket->tryConsume(packetSize))
            return false;
    }

    UeSessionId ueSessionId{ueId, psi};
    if (uplinkBySession.count(ueSessionId))
    {
        auto &bucket = uplinkBySession[ueSessionId];
        if (!bucket->tryConsume(packetSize))
            return false;
    }

    return true;
}

void RateLimiter::updateUeUplinkLimit(int64_t ueId, uint64_t limit)
{
    if (limit <= 0)
    {
        uplinkByUe.erase(ueId);
        return;
    }

    if (uplinkByUe.count(ueId))
    {
        auto &bucket = uplinkByUe[ueId];
        bucket->updateCapacity(limit);
    }
    else
    {
        uplinkByUe[ueId] = std::make_unique<TokenBucket>(limit);
    }
}

void RateLimiter::updateUeDownlinkLimit(int64_t ueId, uint64_t limit)
{
    if (limit <= 0)
    {
        downlinkByUe.erase(ueId);
        return;
    }

    if (downlinkByUe.count(ueId))
    {
        auto &bucket = downlinkByUe[ueId];
        bucket->updateCapacity(limit);
    }
    else
    {
        downlinkByUe[ueId] = std::make_unique<TokenBucket>(limit);
    }
}

void RateLimiter::updateSessionUplinkLimit(int64_t ueId, int psi, uint64_t limit)
{
    UeSessionId ueSessionId{ueId, psi};
    if (limit <= 0)
    {
        uplinkBySession.erase(ueSessionId);
        return;
    }

    if (uplinkBySession.count(ueSessionId))
    {
        auto &bucket = uplinkBySession[ueSessionId];
        bucket->updateCapacity(limit);
    }
    else
    {
        uplinkBySession[ueSessionId] = std::make_unique<TokenBucket>(limit);
    }
}

void RateLimiter::updateSessionDownlinkLimit(int64_t ueId, int psi, uint64_t limit)
{
    UeSessionId ueSessionId{ueId, psi};
    if (limit <= 0)
    {
        downlinkBySession.erase(ueSessionId);
        return;
    }

    if (downlinkBySession.count(ueSessionId))
    {
        auto &bucket = downlinkBySession[ueSessionId];
        bucket->updateCapacity(limit);
    }
    else
    {
        downlinkBySession[ueSessionId] = std::make_unique<TokenBucket>(limit);
    }
}

} // namespace nr::gnb
