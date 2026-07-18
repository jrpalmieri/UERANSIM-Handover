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

// Which half of the shared UE-associated stream space this side allocates from.
// The two gNBs on an Xn link partition the space by gnbId so that handovers
// initiated in opposite directions can never pick the same stream ID (issue 9):
// the lower gnbId allocates EVEN streams (2,4,6,…), the higher gnbId allocates
// ODD streams (3,5,7,…).  A handover therefore always runs on a stream of the
// initiator's parity; the responder merely replies on that same stream and never
// draws it from its own (opposite-parity) allocator.
enum class StreamParity
{
    Even,
    Odd,
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
        // (Overwritten by resetStreams() once the association's stream counts
        //  and this side's parity are known.)
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
        // Only returns IDs this manager actually handed out (in_use).  A stream
        // chosen by the peer (the opposite parity, used only for replying) was
        // never marked in_use here, so releasing it is a safe no-op and cannot
        // pollute this side's parity-partitioned pool.
        if (in_use.test(id)) {
            in_use.reset(id);
            free_pool.push_back(id);
        }
    }

    bool is_active(uint16_t id) const {
        return in_use.test(id);
    }

    // Seed the free pool with this side's half of the [2, min(in,out)) stream
    // space, selected by parity (see StreamParity).  Streams 0/1 are reserved
    // for non-UE-associated signalling and are never pooled.
    bool resetStreams(int inCount, int outCount, StreamParity parity) {
        // Clear all current allocations
        in_use.reset();
        free_pool.clear();

        // Re-populate the pool based on new stream counts.  Fill high→low so the
        // lowest IDs of this parity are popped first (matches the constructor).
        uint16_t maxStream = std::min(static_cast<uint16_t>(inCount), static_cast<uint16_t>(outCount));
        uint16_t first = (parity == StreamParity::Even) ? 2 : 3;
        if (maxStream > first)
        {
            // Highest ID of this parity strictly below maxStream.
            uint16_t last = static_cast<uint16_t>(maxStream - 1);
            if (((last ^ first) & 1u) != 0)
                --last; // step back to the correct parity
            for (int i = last; i >= static_cast<int>(first); i -= 2)
                free_pool.push_back(static_cast<uint16_t>(i));
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

    // Find a peer by its SCTP clientId (outbound connections use the gnbId as
    // clientId; accepted inbound associations use negative task-assigned ids
    // bound to the peer during Xn Setup).  -1 ("unset") never matches.
    XnPeerInfo* findByClientId(int clientId);
    
    bool addPeerInfo(const XnPeerInfo& peerInfo);
    
    // Merge Xn-Setup-learned data (NCI, PCI, PLMN/TAC/AMF-region lists) into the
    // existing entry for info.gnbId, or add a new entry if none exists.  Never
    // touches addr/sctpAssoc/streamIdManager/connectionState, which belong to
    // the connection lifecycle.  Returns the stored entry.
    // NOTE: addPeerInfo/applySetupInfo may reallocate the underlying vector —
    // do not hold XnPeerInfo* from getPeerInfo() across calls to either.
    XnPeerInfo* applySetupInfo(const XnPeerInfo& info);
    
    bool removePeerInfo(int gnbId);
    
    std::vector<XnPeerInfo>& getAllPeers();

    bool updatePeerSctpInfo(int gnbId, SctpAssociation *assoc, int nonUeStreamUplink, int nonUeStreamDownlink);
};



} // namespace nr::gnb