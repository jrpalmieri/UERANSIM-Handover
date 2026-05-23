#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <bitset>
#include <optional>
#include <cstdint>

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <utils/unique_buffer.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/sctp/task.hpp>

extern "C"
{
    struct ASN_XNAP_XnAP_PDU;
}

namespace nr::gnb
{

enum class EXnConnectionState
{
    DISCONNECTED = 0,
    CONNECTION_REQUESTED,
    CONNECTED,
    CONNECTION_FAILED,
};


class StreamIdManager 
{
  private:
    static constexpr size_t MAX_STREAMS = 65536;
    std::bitset<MAX_STREAMS> in_use;
    std::vector<uint16_t> free_pool;

  public:
    StreamIdManager() {
        // Pre-fill the pool with available IDs in reverse order
        // so that lower IDs (like 2, 3, 4...) are popped first.
        free_pool.reserve(MAX_STREAMS);
        for (int i = MAX_STREAMS - 1; i >= 2; --i) {
            free_pool.push_back(static_cast<uint16_t>(i));
        }
    }

    std::optional<uint16_t> allocate() {
        if (free_pool.empty()) return std::nullopt;

        uint16_t id = free_pool.back();
        free_pool.pop_back();
        in_use.set(id);
        return id;
    }

    void release(uint16_t id) {
        if (in_use.test(id)) {
            in_use.reset(id);
            free_pool.push_back(id);
        }
    }

    bool is_active(uint16_t id) const {
        return in_use.test(id);
    }

    bool resetStreams(int inCount, int outCount) {
        // Clear all current allocations
        in_use.reset();
        free_pool.clear();

        // Re-populate the pool based on new stream counts
        uint16_t maxStream = std::min(static_cast<uint16_t>(inCount), static_cast<uint16_t>(outCount));
        for (uint16_t i = 2; i < maxStream; ++i) {
            free_pool.push_back(i);
        }
        return true;
    }
};

// Information about a peer gNB learned from a received XnSetupRequest.
struct XnPeerInfo
{

    InetAddress addr;                 // Ip address and port of the peer gNB
    int clientId{-1};                 // SCTP client ID for this peer (same as gnbId, per updateXnConnections)
    int gnbId{};                      // gnbId for this connection (max 32 bits)
    int64_t nci{-1};                  // Full 36-bit NCI from NR-CGI in ServedCells-NR
    int nrPCI{-1};                    // Physical cell ID (first served NR cell)
    std::vector<Plmn> plmnList;       // Broadcast PLMNs from TAISupport-List
    std::vector<int> tacList;         // TACs from TAISupport-List (24-bit values)
    std::vector<int> amfRegionList;   // 8-bit AMF region IDs from AMF-Region-Information

    SctpAssociation sctpAssoc{};    // SCTP association with this peer, or nullptr if not connected
    int nonUeStream{0};             // SCTP stream for sending/receiving non-UE-Associated messages (should be 0)
    StreamIdManager streamIdManager; // Manager for allocating SCTP stream IDs for UE-associated messages

    EXnConnectionState connectionState{EXnConnectionState::DISCONNECTED};
};


class XnPeerTable
{
private:
    /* data */
    std::vector<XnPeerInfo> m_xnPeerTable;

public:
    XnPeerTable(/* args */);
    ~XnPeerTable();

    XnPeerInfo* getPeerInfo(int gnbId);
    bool addPeerInfo(const XnPeerInfo& peerInfo);
    bool removePeerInfo(int gnbId);
    std::vector<XnPeerInfo>& getAllPeers();

    bool updatePeerSctpInfo(int gnbId, SctpAssociation *assoc, int nonUeStreamUplink, int nonUeStreamDownlink);
};



} // namespace nr::gnb