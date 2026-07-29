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

XnPeerInfo* XnPeerTable::findByClientId(int clientId)
{
    if (clientId == -1)
        return nullptr;
    for (auto& peer : m_xnPeerTable)
    {
        if (peer.clientId == clientId)
            return &peer;
    }
    return nullptr;
}

XnPeerInfo* XnPeerTable::applySetupInfo(const XnPeerInfo& info)
{
    auto *existing = getPeerInfo(info.gnbId);
    if (existing == nullptr)
    {
        m_xnPeerTable.push_back(info);
        return &m_xnPeerTable.back();
    }

    existing->nci           = info.nci;   // authoritative NCI from ServedCells-NR
    existing->nrPCI         = info.nrPCI;
    existing->plmnList      = info.plmnList;
    existing->tacList       = info.tacList;
    existing->amfRegionList = info.amfRegionList;
    return existing;
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



} // namespace nr::gnb
