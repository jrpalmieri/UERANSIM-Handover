#include "peertable.hpp"

namespace nr::gnb
{

XnPeerTable::XnPeerTable() = default;
XnPeerTable::~XnPeerTable() = default;

XnPeerInfo* XnPeerTable::getPeerInfo(int gnbId)
{
    for (auto& peer : m_xnPeerTable)
    {
        if (peer.gnbId == gnbId)
            return &peer;
    }
    return nullptr;
}

bool XnPeerTable::addPeerInfo(const XnPeerInfo& peerInfo)
{
    m_xnPeerTable.push_back(peerInfo);
    return true;
}

bool XnPeerTable::removePeerInfo(int gnbId)
{
    for (auto it = m_xnPeerTable.begin(); it != m_xnPeerTable.end(); ++it)
    {
        if (it->gnbId == gnbId)
        {
            m_xnPeerTable.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<XnPeerInfo>& XnPeerTable::getAllPeers()
{
    return m_xnPeerTable;
}

bool XnPeerTable::updatePeerSctpInfo(int gnbId, SctpAssociation *assoc,
                                      int nonUeStreamUplink, int /*nonUeStreamDownlink*/)
{
    auto *peer = getPeerInfo(gnbId);
    if (peer == nullptr)
        return false;

    if (assoc != nullptr)
        peer->sctpAssoc = *assoc;

    // nonUeStream is the single stream used for non-UE-associated signalling (stream 0).
    // Assumption: use the uplink value; downlink is symmetric on stream 0.
    if (nonUeStreamUplink >= 0)
        peer->nonUeStream = nonUeStreamUplink;

    return true;
}

} // namespace nr::gnb
