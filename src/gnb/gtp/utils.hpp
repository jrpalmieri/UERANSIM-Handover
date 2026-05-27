//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <gnb/types.hpp>

namespace nr::gnb
{

struct UeSessionId {
  int64_t ueId;
  int psi;

  bool operator==(const UeSessionId &other) const {
    return ueId == other.ueId && psi == other.psi;
  }
};

struct UeSessionIdHash {
    std::size_t operator()(const UeSessionId& usi) const {
        // Hash both individual values
        std::size_t h1 = std::hash<int64_t>{}(usi.ueId);
        std::size_t h2 = std::hash<int>{}(usi.psi);
        
        // Combine them using the standard bitwise hash-combine approach
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};


class PduSessionTree
{
  private:
    // map the downlink tunnelId to a UE ID and PDU session Id
    std::unordered_map<uint32_t, UeSessionId> mapByDownTeid;
    //std::unordered_map<uint32_t, uint64_t> mapByDownTeid;

    // map the UE ID and PDU session Id to the tunnelId
    // std::unordered_map<UeSessionId, uint32_t, UeSessionIdHash> mapByUePsId;

    // map the UE ID to PduSessionResource structs
    std::unordered_map<int64_t, std::vector<PduSessionResource>> mapByUeId;

  public:
    PduSessionTree();
    void insertSession(int64_t ueId, int psi, PduSessionResource &session);
    UeSessionId* findByDownTeid(uint32_t teid);
    uint64_t findBySessionId(int64_t ue, int psi);
    int getSession(int64_t ueId, int psi, PduSessionResource *&session);
    void removeSession(int64_t ueId, int psi);
    void removeAllSessions(int64_t ueId);
    void enumerateByUe(int64_t ue, std::vector<PduSessionResource> &output);
};

class TokenBucket
{
    static constexpr const int64_t REFILL_PERIOD = 1000L;

    uint64_t byteCapacity;
    double refillTokensPerOneMillis;
    double availableTokens;
    int64_t lastRefillTimestamp;

  public:
    explicit TokenBucket(int64_t byteCapacity);

    bool tryConsume(uint64_t numberOfTokens);
    void updateCapacity(uint64_t newByteCapacity);

  private:
    void refill();
};

class IRateLimiter
{
  public:
    virtual bool allowDownlinkPacket(int64_t ueId, int psi, uint64_t packetSize) = 0;
    virtual bool allowUplinkPacket(int64_t ueId, int psi, uint64_t packetSize) = 0;
    virtual void updateUeUplinkLimit(int64_t ueId, uint64_t limit) = 0;
    virtual void updateUeDownlinkLimit(int64_t ueId, uint64_t limit) = 0;
    virtual void updateSessionUplinkLimit(int64_t ueId, int psi, uint64_t limit) = 0;
    virtual void updateSessionDownlinkLimit(int64_t ueId, int psi, uint64_t limit) = 0;
};

class RateLimiter : public IRateLimiter
{
    std::unordered_map<int64_t, std::unique_ptr<TokenBucket>> downlinkByUe;
    std::unordered_map<int64_t, std::unique_ptr<TokenBucket>> uplinkByUe;
    std::unordered_map<UeSessionId, std::unique_ptr<TokenBucket>, UeSessionIdHash> downlinkBySession;
    std::unordered_map<UeSessionId, std::unique_ptr<TokenBucket>, UeSessionIdHash> uplinkBySession;

  public:
    bool allowDownlinkPacket(int64_t ueId, int psi, uint64_t packetSize) override;
    bool allowUplinkPacket(int64_t ueId, int psi, uint64_t packetSize) override;
    void updateUeUplinkLimit(int64_t ueId, uint64_t limit) override;
    void updateUeDownlinkLimit(int64_t ueId, uint64_t limit) override;
    void updateSessionUplinkLimit(int64_t ueId, int psi, uint64_t limit) override;
    void updateSessionDownlinkLimit(int64_t ueId, int psi, uint64_t limit) override;
};

} // namespace nr::gnb
