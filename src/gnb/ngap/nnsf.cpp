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

NgapAmfContext *NgapTask::selectAmf(int ueId, int32_t &requestedSliceType)
{
    // First pass: try to match by requested slice type
    for (auto &amf : m_amfCtx) {
        for (const auto &plmnSupport : amf.second->plmnSupportList) {
            for (const auto &singleSlice : plmnSupport->sliceSupportList.slices) {
                int32_t supportedSliceType = static_cast<int32_t>(singleSlice.sst);
                if (supportedSliceType == requestedSliceType) {
                    return amf.second;
                }
            }
        }
    }

    // Fallback: if no slice match (e.g. NAS without NSSAI), return first AMF
    if (!m_amfCtx.empty())
    {
        m_logger->warn("UE[%d] No slice-based AMF match (requestedSST=%d), using first available AMF",
                       ueId, requestedSliceType);
        return m_amfCtx.begin()->second;
    }

    return nullptr;
}

NgapAmfContext *NgapTask::selectNewAmfForReAllocation(int ueId, int initiatedAmfId, int amfSetId)
{
    // TODO an arbitrary AMF is selected for now
    return findAmfContext(initiatedAmfId);
}

} // namespace nr::gnb
