//
// NGAP Handover Procedures (N2 / AMF-mediated)
//
// Implements:
//   - sendHandoverRequired()           → source gNB → AMF
//   - receiveHandoverCommand()         ← AMF → source gNB (SuccessfulOutcome)
//   - receiveHandoverPreparationFailure() ← AMF → source gNB (UnsuccessfulOutcome)
//   - sendHandoverNotify()             → target gNB → AMF
//   - sendPathSwitchRequest()          → target gNB → AMF
//   - receivePathSwitchRequestAcknowledge() ← AMF → target gNB
//   - receivePathSwitchRequestFailure()     ← AMF → target gNB
//   - handleHandoverNotifyFromRrc()    (orchestrator called from task.cpp)
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/gtp/task.hpp>
#include <gnb/neighbors.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/xn/task.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>

#include <asn/ngap/ASN_NGAP_Cause.h>
#include <asn/ngap/ASN_NGAP_GlobalGNB-ID.h>
#include <asn/ngap/ASN_NGAP_GlobalRANNodeID.h>
#include <asn/ngap/ASN_NGAP_GNB-ID.h>
#include <asn/ngap/ASN_NGAP_HandoverCommand.h>
#include <asn/ngap/ASN_NGAP_HandoverFailure.h>
#include <asn/ngap/ASN_NGAP_HandoverRequiredTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverRequest.h>
#include <asn/ngap/ASN_NGAP_HandoverRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_HandoverRequestAcknowledgeTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverNotify.h>
#include <asn/ngap/ASN_NGAP_HandoverPreparationFailure.h>
#include <asn/ngap/ASN_NGAP_HandoverRequired.h>
#include <asn/ngap/ASN_NGAP_HandoverType.h>
#include <asn/ngap/ASN_NGAP_NGAP-PDU.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequest.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestFailure.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceAdmittedItem.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceAdmittedList.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupListHOAck.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceItemHORqd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceListHORqd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemHOReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequestTransfer.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_QosFlowItemWithDataForwarding.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/ngap/ASN_NGAP_SourceToTarget-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_SuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_TAI.h>
#include <asn/ngap/ASN_NGAP_TargetID.h>
#include <asn/ngap/ASN_NGAP_TargetRANNodeID.h>
#include <asn/ngap/ASN_NGAP_TargetToSource-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_UPTransportLayerInformation.h>
#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_UnsuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformationNR.h>
#include <asn/ngap/ASN_NGAP_NR-CGI.h>

#include <asn/rrc/ASN_RRC_AS-Context.h>
#include <asn/rrc/ASN_RRC_AS-Config.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_HandoverPreparationInformation-IEs.h>
#include <asn/rrc/ASN_RRC_HandoverPreparationInformation.h>
#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_ReestablishmentInfo.h>

#include <optional>

const int HANDOVER_TIMEOUT_MS = 5000;

namespace nr::gnb
{

static constexpr uint32_t CUSTOM_S2T_MAGIC = 0x53325443; // "S2TC"
static constexpr uint8_t CUSTOM_S2T_VERSION = 1;
static constexpr uint32_t CUSTOM_S2T_DEFAULT_BLOB_SIZE = 0;
static constexpr uint32_t CUSTOM_T2S_MAGIC = 0x54325343; // "T2SC"
static constexpr uint8_t CUSTOM_T2S_VERSION = 1;
static constexpr uint32_t CUSTOM_T2S_DEFAULT_BLOB_SIZE = 0;
static constexpr uint8_t CUSTOM_S2T_FLAG_CHO_INDICATION = 0x01;
static constexpr int CHO_CANDIDATE_TIMEOUT_MS = 60000;

static bool ExtractTargetPciFromHandoverCommandRrcContainer(const OctetString &rrcContainer, int &targetPci)
{
    auto *decoded =
        rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, rrcContainer);
    if (!decoded)
        return false;

    bool ok = false;

    bool isReconfig = decoded->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
                      decoded->message.choice.c1 != nullptr &&
                      decoded->message.choice.c1->present ==
                          ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                      decoded->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (isReconfig)
    {
        auto *reconfig = decoded->message.choice.c1->choice.rrcReconfiguration;
        if (reconfig->criticalExtensions.present ==
                ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration &&
            reconfig->criticalExtensions.choice.rrcReconfiguration != nullptr)
        {
            auto *ies = reconfig->criticalExtensions.choice.rrcReconfiguration;
            if (ies->nonCriticalExtension != nullptr && ies->nonCriticalExtension->masterCellGroup != nullptr)
            {
                OctetString masterCellGroup = asn::GetOctetString(*ies->nonCriticalExtension->masterCellGroup);
                auto *cellGroup =
                    rrc::encode::Decode<ASN_RRC_CellGroupConfig>(asn_DEF_ASN_RRC_CellGroupConfig, masterCellGroup);
                if (cellGroup)
                {
                    if (cellGroup->spCellConfig != nullptr &&
                        cellGroup->spCellConfig->reconfigurationWithSync != nullptr &&
                        cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon != nullptr &&
                        cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon->physCellId != nullptr)
                    {
                        targetPci = static_cast<int>(
                            *cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon->physCellId);
                        ok = true;
                    }

                    asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, cellGroup);
                }
            }
        }
    }

    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, decoded);
    return ok;
}

struct CustomS2tTransparentContainer
{
    OctetString rrcContext{};
    OctetString ngapContext{};
    OctetString gtpContext{};
    OctetString blob{};

    int sourceUeId{-1};
    int64_t sourceAmfUeNgapId{-1};
    int64_t sourceRanUeNgapId{-1};
    int sourceAssociatedAmfId{-1};
    int sourceUplinkStream{0};
    int sourceDownlinkStream{0};
    AggregateMaximumBitRate sourceUeAmbr{};
    HandoverPreparationInfo handoverPreparation{};
    bool choIndication{};
};

static OctetString EncodeRrcReconfigurationMeasIds(const HandoverPreparationInfo &handoverPrep)
{
    if (handoverPrep.measConfigRrcReconfiguration.length() > 0)
        return handoverPrep.measConfigRrcReconfiguration.copy();

    if (handoverPrep.measIdentities.empty())
        return OctetString{};

    auto *msg = asn::New<ASN_RRC_DL_DCCH_Message>();
    msg->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    msg->message.choice.c1 = asn::NewFor(msg->message.choice.c1);
    msg->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = msg->message.choice.c1->choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration>();
    reconfig->rrc_TransactionIdentifier = 0;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

    auto &ies =
        reconfig->criticalExtensions.choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration_IEs>();
    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    ies->measConfig->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();

    for (const auto &item : handoverPrep.measIdentities)
    {
        auto *entry = asn::New<ASN_RRC_MeasIdToAddMod>();
        entry->measId = item.measId;
        entry->measObjectId = item.measObjectId;
        entry->reportConfigId = item.reportConfigId;
        asn::SequenceAdd(*ies->measConfig->measIdToAddModList, entry);
    }

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, msg);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, msg);
    return encoded;
}


// this does nothing.  In future could be used to pass measConfig data to target
static std::vector<HandoverMeasurementIdentity> DecodeRrcReconfigurationMeasIds(const OctetString &encoded)
{
    std::vector<HandoverMeasurementIdentity> out{};
    if (encoded.length() == 0)
        return out;

    auto *msg = rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, encoded);
    if (!msg)
        return out;

    bool valid = msg->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 && msg->message.choice.c1 != nullptr &&
                 msg->message.choice.c1->present == ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                 msg->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (valid)
    {
        auto *reconfig = msg->message.choice.c1->choice.rrcReconfiguration;
        auto *ies = reconfig->criticalExtensions.present ==
                            ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration
                        ? reconfig->criticalExtensions.choice.rrcReconfiguration
                        : nullptr;

        auto *measList = (ies && ies->measConfig) ? ies->measConfig->measIdToAddModList : nullptr;
        if (measList)
        {
            out.reserve(measList->list.count);
            for (int i = 0; i < measList->list.count; ++i)
            {
                auto *entry = measList->list.array[i];
                if (!entry)
                    continue;

                out.push_back({entry->measId, entry->measObjectId, entry->reportConfigId, nr::rrc::common::HandoverEventType::UNKNOWN, "transferred"});
            }
        }
    }

    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, msg);
    return out;
}

static OctetString EncodeCustomNgapContext(const NgapUeContext &ue, int sourceUeId)
{
    OctetString ngapContext{};
    ngapContext.appendOctet4(sourceUeId);
    ngapContext.appendOctet8(ue.amfUeNgapId);
    ngapContext.appendOctet8(ue.ranUeNgapId);
    ngapContext.appendOctet4(ue.associatedAmfId);
    ngapContext.appendOctet4(ue.uplinkStream);
    ngapContext.appendOctet4(ue.downlinkStream);
    return ngapContext;
}

static OctetString EncodeCustomGtpContext(const NgapUeContext &ue)
{
    OctetString gtpContext{};
    gtpContext.appendOctet8(ue.ueAmbr.dlAmbr);
    gtpContext.appendOctet8(ue.ueAmbr.ulAmbr);
    return gtpContext;
}

static bool DecodeCustomNgapContext(const OctetString &ngapContext, CustomS2tTransparentContainer &decoded)
{
    // sourceUeId(4) + amfUeNgapId(8) + ranUeNgapId(8) + amfId(4) + ulStream(4) + dlStream(4)
    if (ngapContext.length() < 32)
        return false;

    decoded.sourceUeId = static_cast<int>(ngapContext.get4UI(0));
    decoded.sourceAmfUeNgapId = static_cast<int64_t>(ngapContext.get8UL(4));
    decoded.sourceRanUeNgapId = static_cast<int64_t>(ngapContext.get8UL(12));
    decoded.sourceAssociatedAmfId = static_cast<int>(ngapContext.get4UI(20));
    decoded.sourceUplinkStream = static_cast<int>(ngapContext.get4UI(24));
    decoded.sourceDownlinkStream = static_cast<int>(ngapContext.get4UI(28));
    return true;
}

static bool DecodeCustomGtpContext(const OctetString &gtpContext, CustomS2tTransparentContainer &decoded)
{
    // dlAmbr(8) + ulAmbr(8)
    if (gtpContext.length() < 16)
        return false;

    decoded.sourceUeAmbr.dlAmbr = gtpContext.get8UL(0);
    decoded.sourceUeAmbr.ulAmbr = gtpContext.get8UL(8);
    return true;
}

static std::optional<CustomS2tTransparentContainer> DecodeCustomSourceToTargetTransparentContainer(
    const OctetString &encoded)
{
    // magic(4) + version(1) + flags(1) + reserved(2) + rrcLen(4) + ngapLen(4) + gtpLen(4) + blobLen(4)
    if (encoded.length() < 24)
        return std::nullopt;

    uint32_t magic = encoded.get4UI(0);
    uint8_t version = static_cast<uint8_t>(encoded.getI(4));
    uint8_t flags = static_cast<uint8_t>(encoded.getI(5));

    if (magic != CUSTOM_S2T_MAGIC || version != CUSTOM_S2T_VERSION)
        return std::nullopt;

    uint32_t rrcLen = encoded.get4UI(8);
    uint32_t ngapLen = encoded.get4UI(12);
    uint32_t gtpLen = encoded.get4UI(16);
    uint32_t blobLen = encoded.get4UI(20);

    uint64_t payloadLen = static_cast<uint64_t>(rrcLen) + static_cast<uint64_t>(ngapLen) +
                          static_cast<uint64_t>(gtpLen) + static_cast<uint64_t>(blobLen);
    uint64_t expectedTotalLen = 24ull + payloadLen;

    if (expectedTotalLen != static_cast<uint64_t>(encoded.length()))
        return std::nullopt;

    int offset = 24;
    CustomS2tTransparentContainer decoded{};
    decoded.rrcContext = encoded.subCopy(offset, static_cast<int>(rrcLen));
    offset += static_cast<int>(rrcLen);

    decoded.ngapContext = encoded.subCopy(offset, static_cast<int>(ngapLen));
    offset += static_cast<int>(ngapLen);

    decoded.gtpContext = encoded.subCopy(offset, static_cast<int>(gtpLen));
    offset += static_cast<int>(gtpLen);

    decoded.blob = encoded.subCopy(offset, static_cast<int>(blobLen));

    if (!DecodeCustomNgapContext(decoded.ngapContext, decoded))
        return std::nullopt;

    if (!DecodeCustomGtpContext(decoded.gtpContext, decoded))
        return std::nullopt;

    decoded.choIndication = (flags & CUSTOM_S2T_FLAG_CHO_INDICATION) != 0;

    return decoded;
}

static OctetString MakeTargetToSourceTransparentContainer(const OctetString &rrcContainer, uint32_t blobSize)
{
    if (rrcContainer.length() == 0)
        return OctetString{};

    auto blob = OctetString::FromSpare(static_cast<int>(blobSize));

    OctetString encoded{};
    encoded.appendOctet4(CUSTOM_T2S_MAGIC);
    encoded.appendOctet(CUSTOM_T2S_VERSION);
    encoded.appendOctet(0); // flags
    encoded.appendOctet2(0); // reserved
    encoded.appendOctet4(static_cast<uint32_t>(rrcContainer.length()));
    encoded.appendOctet4(0); // ngapLen
    encoded.appendOctet4(0); // gtpLen
    encoded.appendOctet4(static_cast<uint32_t>(blob.length()));
    encoded.append(rrcContainer);
    encoded.append(blob);

    return encoded;
}

static std::optional<OctetString> DecodeTargetToSourceTransparentContainer(const OctetString &encoded)
{
    // magic(4) + version(1) + flags(1) + reserved(2) + rrcLen(4) + ngapLen(4) + gtpLen(4) + blobLen(4)
    if (encoded.length() < 24)
        return std::nullopt;

    uint32_t magic = encoded.get4UI(0);
    uint8_t version = static_cast<uint8_t>(encoded.getI(4));
    if (magic != CUSTOM_T2S_MAGIC || version != CUSTOM_T2S_VERSION)
        return std::nullopt;

    uint32_t rrcLen = encoded.get4UI(8);
    uint32_t ngapLen = encoded.get4UI(12);
    uint32_t gtpLen = encoded.get4UI(16);
    uint32_t blobLen = encoded.get4UI(20);

    uint64_t payloadLen = static_cast<uint64_t>(rrcLen) + static_cast<uint64_t>(ngapLen) +
                          static_cast<uint64_t>(gtpLen) + static_cast<uint64_t>(blobLen);
    uint64_t expectedTotalLen = 24ull + payloadLen;
    if (expectedTotalLen != static_cast<uint64_t>(encoded.length()))
        return std::nullopt;

    if (rrcLen == 0)
        return std::nullopt;

    return encoded.subCopy(24, static_cast<int>(rrcLen));
}

static OctetString MakeHoSetupUnsuccessfulTransfer(NgapCause cause)
{
    auto *tr = asn::New<ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer>();
    ngap_utils::ToCauseAsn_Ref(cause, tr->cause);

    OctetString encoded =
        ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer, tr);

    return encoded;
}

static OctetString MakeHoAcknowledgeTransfer(const PduSessionResource &resource)
{
    auto *tr = asn::New<ASN_NGAP_HandoverRequestAcknowledgeTransfer>();

    auto &upInfo = tr->dL_NGU_UP_TNLInformation;
    upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
    upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
    asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource.downTunnel.address);
    asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource.downTunnel.teid);

    if (resource.qosFlows)
    {
        auto &qosList = resource.qosFlows->list;
        for (int i = 0; i < qosList.count; i++)
        {
            auto *qosItem = asn::New<ASN_NGAP_QosFlowItemWithDataForwarding>();
            qosItem->qosFlowIdentifier = qosList.array[i]->qosFlowIdentifier;
            asn::SequenceAdd(tr->qosFlowSetupResponseList, qosItem);
        }
    }

    OctetString encoded = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverRequestAcknowledgeTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverRequestAcknowledgeTransfer, tr);

    return encoded;
}

static OctetString MakeHoRequiredTransfer()
{
    // Open5GS expects a present and decodable transfer blob per PDU session item.
    auto *tr = asn::New<ASN_NGAP_HandoverRequiredTransfer>();
    OctetString encoded = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverRequiredTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverRequiredTransfer, tr);
    return encoded;
}

static OctetString MakeRrcHandoverPreparationInformation(int sourcePci,
                                                         const HandoverPreparationInfo &handoverPrep)
{
    auto *msg = asn::New<ASN_RRC_HandoverPreparationInformation>();
    msg->criticalExtensions.present = ASN_RRC_HandoverPreparationInformation__criticalExtensions_PR_c1;
    msg->criticalExtensions.choice.c1 = asn::NewFor(msg->criticalExtensions.choice.c1);
    msg->criticalExtensions.choice.c1->present =
        ASN_RRC_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation;

    auto &ies = msg->criticalExtensions.choice.c1->choice.handoverPreparationInformation =
        asn::New<ASN_RRC_HandoverPreparationInformation_IEs>();

    ies->as_Context = asn::New<ASN_RRC_AS_Context>();
    ies->as_Context->reestablishmentInfo = asn::New<ASN_RRC_ReestablishmentInfo>();
    ies->as_Context->reestablishmentInfo->sourcePhysCellId = static_cast<long>(sourcePci);
    asn::SetBitStringInt<16>(0, ies->as_Context->reestablishmentInfo->targetCellShortMAC_I);

    OctetString reconfig = EncodeRrcReconfigurationMeasIds(handoverPrep);
    if (reconfig.length() > 0)
    {
        ies->sourceConfig = asn::New<ASN_RRC_AS_Config>();
        asn::SetOctetString(ies->sourceConfig->rrcReconfiguration, reconfig);
    }

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_HandoverPreparationInformation, msg);
    asn::Free(asn_DEF_ASN_RRC_HandoverPreparationInformation, msg);
    return encoded;
}

static OctetString MakeSourceToTargetTransparentContainer(const NgapUeContext &ue, int sourceUeId,
                                                          int sourcePci, uint32_t blobSize,
                                                          const HandoverPreparationInfo &handoverPrep,
                                                          bool choIndication)
{
    auto rrcContext = MakeRrcHandoverPreparationInformation(sourcePci, handoverPrep);
    if (rrcContext.length() == 0)
        return OctetString{};

    auto ngapContext = EncodeCustomNgapContext(ue, sourceUeId);
    auto gtpContext = EncodeCustomGtpContext(ue);
    auto blob = OctetString::FromSpare(static_cast<int>(blobSize));

    OctetString encoded{};
    encoded.appendOctet4(CUSTOM_S2T_MAGIC);
    encoded.appendOctet(CUSTOM_S2T_VERSION);
    // indicator that this a ConditionalHandover (CHO) request
    uint8_t flags = choIndication ? CUSTOM_S2T_FLAG_CHO_INDICATION : 0;
    encoded.appendOctet(flags);
    encoded.appendOctet2(0); // reserved

    // relevant UE context information
    encoded.appendOctet4(static_cast<uint32_t>(rrcContext.length()));
    encoded.appendOctet4(static_cast<uint32_t>(ngapContext.length()));
    encoded.appendOctet4(static_cast<uint32_t>(gtpContext.length()));
    encoded.appendOctet4(static_cast<uint32_t>(blob.length()));
    encoded.append(rrcContext);
    encoded.append(ngapContext);
    encoded.append(gtpContext);
    encoded.append(blob);

    return encoded;
}

/**
 * @brief Logs the contents of a SourceToTarget transparent container.
 * 
 * @param logger The logger to use for output
 * @param container The transparent container to log
 */
static void LogRrcHandoverPreparationInformation(Logger &logger, const OctetString &rrcContext,
                                                 HandoverPreparationInfo *handoverPrep)
{
    auto *msg = rrc::encode::Decode<ASN_RRC_HandoverPreparationInformation>(
        asn_DEF_ASN_RRC_HandoverPreparationInformation, rrcContext);
    if (!msg)
    {
        logger.warn("Custom SourceToTarget RRC context decode failed as HandoverPreparationInformation");
        return;
    }

    auto *c1 = msg->criticalExtensions.choice.c1;
    bool isHpi = msg->criticalExtensions.present == ASN_RRC_HandoverPreparationInformation__criticalExtensions_PR_c1 &&
                 c1 != nullptr &&
                 c1->present ==
                     ASN_RRC_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation &&
                 c1->choice.handoverPreparationInformation != nullptr;

    if (!isHpi)
    {
        logger.warn("SourceToTarget container decoded but did not contain handoverPreparationInformation");
        asn::Free(asn_DEF_ASN_RRC_HandoverPreparationInformation, msg);
        return;
    }

    auto *ies = c1->choice.handoverPreparationInformation;
    long sourcePhysCellId = -1;
    int targetShortMac = -1;

    if (ies->as_Context != nullptr && ies->as_Context->reestablishmentInfo != nullptr)
    {
        sourcePhysCellId = ies->as_Context->reestablishmentInfo->sourcePhysCellId;
        targetShortMac = asn::GetBitStringInt<16>(ies->as_Context->reestablishmentInfo->targetCellShortMAC_I);
    }

    logger.info("Decoded SourceToTarget container: ueCapCount=%d sourceConfig=%d rrmConfig=%d "
                "asContext=%d sourcePhysCellId=%ld targetShortMac=0x%04x",
                ies->ue_CapabilityRAT_List.list.count,
                ies->sourceConfig != nullptr,
                ies->rrm_Config != nullptr,
                ies->as_Context != nullptr,
                sourcePhysCellId,
                targetShortMac >= 0 ? targetShortMac : 0);

    if (handoverPrep != nullptr && ies->sourceConfig != nullptr)
    {
        OctetString encodedReconfig = asn::GetOctetString(ies->sourceConfig->rrcReconfiguration);
        handoverPrep->measIdentities = DecodeRrcReconfigurationMeasIds(encodedReconfig);
        logger.info("Decoded %zu measurement identities from handoverPreparationInformation",
                    handoverPrep->measIdentities.size());
    }

    asn::Free(asn_DEF_ASN_RRC_HandoverPreparationInformation, msg);
}

static bool LogAndDecodeCustomSourceToTargetTransparentContainer(Logger &logger, const OCTET_STRING_t &container,
                                                                 CustomS2tTransparentContainer &decoded)
{
    auto sourceToTarget = asn::GetOctetString(container);
    auto maybeDecoded = DecodeCustomSourceToTargetTransparentContainer(sourceToTarget);
    if (!maybeDecoded.has_value())
    {
        logger.warn("Custom SourceToTarget transparent container decode failed");
        return false;
    }

    decoded = std::move(*maybeDecoded);
    logger.info("Decoded custom SourceToTarget container: sourceUeId=%d amfUeNgapId=%ld ranUeNgapId=%ld "
                "amfId=%d ulStream=%d dlStream=%d dlAmbr=%lu ulAmbr=%lu cho=%d rrc=%dB ngap=%dB gtp=%dB "
                "blob=%dB",
                decoded.sourceUeId,
                decoded.sourceAmfUeNgapId,
                decoded.sourceRanUeNgapId,
                decoded.sourceAssociatedAmfId,
                decoded.sourceUplinkStream,
                decoded.sourceDownlinkStream,
                static_cast<unsigned long>(decoded.sourceUeAmbr.dlAmbr),
                static_cast<unsigned long>(decoded.sourceUeAmbr.ulAmbr),
                decoded.choIndication ? 1 : 0,
                decoded.rrcContext.length(),
                decoded.ngapContext.length(),
                decoded.gtpContext.length(),
                decoded.blob.length());

    LogRrcHandoverPreparationInformation(logger, decoded.rrcContext, &decoded.handoverPreparation);
    return true;
}

/**
 * @brief Target GNB receives the HandoverRequest from the AMF to initiate the handover procedure
 * for a specific UE. The HandoverRequest includes the AMF-UE-NGAP-ID to identify the UE context, 
 * the HandoverType to indicate the type of handover (e.g., intra-5GS), a SourceToTarget-TransparentContainer
 * that carries the RRC container from the source gNB, which contains necessary information for 
 * the target gNB to prepare for the handover (e.g., the source cell ID, UE capabilities, etc.), 
 * and optionally a list of PDU sessions to be set up during the handover. Upon receiving the 
 * HandoverRequest, the target gNB should process the information, prepare UE context at the target cell
 * and respond with a HandoverRequestAcknowledge if the preparation is successful 
 * or a HandoverPreparationFailure if it fails.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveHandoverRequest(int amfId, ASN_NGAP_HandoverRequest *msg)
{
    auto *amf = findAmfContext(amfId);
    if (!amf)
    {
        m_logger->err("receiveHandoverRequest: AMF context not found");
        return;
    }

    auto *ieAmfUeNgapId = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID);
    if (!ieAmfUeNgapId)
    {
        m_logger->err("receiveHandoverRequest: mandatory AMF_UE_NGAP_ID missing");
        sendErrorIndication(amfId, NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message);
        return;
    }

    int64_t amfUeNgapId = asn::GetSigned64(ieAmfUeNgapId->AMF_UE_NGAP_ID);
    if (amfUeNgapId <= 0)
    {
        m_logger->err("receiveHandoverRequest: invalid AMF_UE_NGAP_ID=%ld", amfUeNgapId);
        sendErrorIndication(amfId, NgapCause::Protocol_semantic_error);
        return;
    }

    auto *ieType = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_HandoverType);
    auto *ieCause = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_Cause);
    auto *ieContainer =
        asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer);
    auto *iePsList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListHOReq);

    int hoType = ieType ? static_cast<int>(ieType->HandoverType) : -1;
    int causePresent = ieCause ? static_cast<int>(ieCause->Cause.present) : -1;
    size_t sourceToTargetSize = ieContainer ? ieContainer->SourceToTarget_TransparentContainer.size : 0;
    int requestedPsCount = iePsList ? iePsList->PDUSessionResourceSetupListHOReq.list.count : 0;

    m_logger->info("HandoverRequest received from AMF[%d] AMF-UE-NGAP-ID=%ld hoType=%d "
                   "s2tContainer=%zuB pduSessionCount=%d causePresent=%d",
                   amfId, amfUeNgapId, hoType, sourceToTargetSize, requestedPsCount, causePresent);

    if (!ieType)
    {
        m_logger->err("receiveHandoverRequest: mandatory HandoverType missing");
        sendErrorIndication(amfId, NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message);
        return;
    }

    if (ieType->HandoverType != ASN_NGAP_HandoverType_intra5gs)
    {
        m_logger->warn("receiveHandoverRequest: unsupported HandoverType=%d", (int)ieType->HandoverType);
        sendErrorIndication(amfId, NgapCause::Protocol_message_not_compatible_with_receiver_state);
        return;
    }

    if (!ieContainer)
    {
        m_logger->err("receiveHandoverRequest: mandatory SourceToTarget_TransparentContainer missing");
        sendErrorIndication(amfId, NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message);
        return;
    }

    CustomS2tTransparentContainer transferredCtx{};
    if (!LogAndDecodeCustomSourceToTargetTransparentContainer(*m_logger,
                                                              ieContainer->SourceToTarget_TransparentContainer,
                                                              transferredCtx))
    {
        sendErrorIndication(amfId, NgapCause::Protocol_transfer_syntax_error);
        return;
    }

    if (transferredCtx.choIndication)
    {
        m_logger->info("receiveHandoverRequest: CHO indication detected in SourceToTarget container");
    }

    // get the ueId from the transferred context
    int ueId = transferredCtx.sourceUeId;
    if (ueId <= 0)
    {
        m_logger->err("receiveHandoverRequest: invalid sourceUeId=%d", ueId);
        sendErrorIndication(amfId, NgapCause::Protocol_semantic_error);
        return;
    }

    /* create a new NGAP UE context*/

    // check if a handover_pending already exists for this UE Id
    if (m_handoverPending.count(ueId))
    {
        m_logger->warn("HandoverRequest received for UE ID %d which is already pending handover; overwriting", ueId);
        auto *oldPending = m_handoverPending[ueId];
        if (oldPending)
        {
            delete oldPending->ctx;
            delete oldPending;
        }
        m_handoverPending.erase(ueId);
    }

    // Create a new UE NGAP context for this handover, and add to the handover pending map.

    auto *ue = new NgapUeContext(ueId);
    ue->associatedAmfId = amfId;
    ue->amfUeNgapId = amfUeNgapId;
    ue->ranUeNgapId = generateRanUeNgapId(ueId);
    ue->ueAmbr = transferredCtx.sourceUeAmbr;

    if (amf->association.outStreams > 0)
    {
        amf->nextStream = (amf->nextStream + 1) % amf->association.outStreams;
        if ((amf->nextStream == 0) && (amf->association.outStreams > 1))
            amf->nextStream += 1;
        ue->uplinkStream = amf->nextStream;
    }
    else
    {
        // Association exists but stream info is missing; preserve UE context and use stream 0 as fallback.
        ue->uplinkStream = 0;
    }

    // add to NGAP pending handover map

    auto *hoPending = new NGAPHandoverPending();
    hoPending->ctx = ue;
    hoPending->id = ueId;
    hoPending->choCandidate = transferredCtx.choIndication;
    // Note: expireTime is different for Conditional Handover (CHO), which is expected to happen much later than a
    //  classical RSRP based handover
    hoPending->expireTime =
        utils::CurrentTimeMillis() + (transferredCtx.choIndication ? CHO_CANDIDATE_TIMEOUT_MS : HANDOVER_TIMEOUT_MS);
    m_handoverPending[ueId] = hoPending;

    if (hoPending->choCandidate)
    {
        m_logger->info("UE[%d] Target side candidate context stored for CHO (extended timeout=%dms)",
                       ue->ctxId,
                       CHO_CANDIDATE_TIMEOUT_MS);
    }

    // add to RRC pending handover map and obtain the target RRC container to be used in the 
    //  HandoverRequestAcknowledge message later.
    OctetString targetRrcContainer{};
    if (!m_base->rrcTask->addPendingHandover(ueId, transferredCtx.handoverPreparation, targetRrcContainer))
    {
        m_logger->warn("UE[%d] HandoverRequest failed: failed to add RRC pending handover context",
                       ue->ctxId);

        if (m_handoverPending.count(ueId))
        {
            auto *pending = m_handoverPending[ueId];
            if (pending)
            {
                delete pending->ctx;
                delete pending;
            }
            m_handoverPending.erase(ueId);
        }
        return;
    }


    /* User Plane Setup */

    // send msg to GTP to create a UE context.
    auto update = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_UPDATE);
    update->update = std::make_unique<GtpUeContextUpdate>(true, ue->ctxId, ue->ueAmbr);
    m_base->gtpTask->push(std::move(update));

    auto sendHandoverFailure = [&](NgapCause cause, const char *reason) {
        if (reason)
            m_logger->warn("UE[%d] HandoverRequest failed: %s", ue->ctxId, reason);

        auto *causeIe = asn::New<ASN_NGAP_HandoverFailureIEs>();
        causeIe->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
        causeIe->criticality = ASN_NGAP_Criticality_ignore;
        causeIe->value.present = ASN_NGAP_HandoverFailureIEs__value_PR_Cause;
        ngap_utils::ToCauseAsn_Ref(cause, causeIe->value.choice.Cause);

        auto *failurePdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverFailure>({causeIe});
        sendNgapUeAssociated(ue->ctxId, failurePdu);
    };

    if (!iePsList || iePsList->PDUSessionResourceSetupListHOReq.list.count == 0)
    {
        sendHandoverFailure(NgapCause::Protocol_semantic_error, "PDUSessionResourceSetupListHOReq missing or empty");
        return;
    }

    std::vector<ASN_NGAP_PDUSessionResourceAdmittedItem *> admittedList;
    std::vector<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck *> failedList;

    auto &hoList = iePsList->PDUSessionResourceSetupListHOReq.list;
    for (int i = 0; i < hoList.count; i++)
    {
        auto &item = hoList.array[i];
        if (!item)
            continue;

        m_logger->debug("UE[%d] HO PDU session[%d]: processing", ue->ctxId, (int)item->pDUSessionID);

        auto *transfer = ngap_encode::Decode<ASN_NGAP_PDUSessionResourceSetupRequestTransfer>(
            asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, item->handoverRequestTransfer);

        if (!transfer)
        {
            m_logger->warn("UE[%d] HO PDU session %d: failed to decode handoverRequestTransfer", ue->ctxId,
                           (int)item->pDUSessionID);

            OctetString encodedFail =
                MakeHoSetupUnsuccessfulTransfer(NgapCause::Protocol_transfer_syntax_error);

            auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
            failed->pDUSessionID = item->pDUSessionID;
            asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
            failedList.push_back(failed);
            continue;
        }

        auto *resource = new PduSessionResource(ue->ctxId, static_cast<int>(item->pDUSessionID));

        auto *ie = asn::ngap::GetProtocolIe(transfer,
                                            ASN_NGAP_ProtocolIE_ID_id_PDUSessionAggregateMaximumBitRate);
        if (ie)
        {
            resource->sessionAmbr.dlAmbr =
                asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateDL) / 8ull;
            resource->sessionAmbr.ulAmbr =
                asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateUL) / 8ull;
        }

        ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_DataForwardingNotPossible);
        if (ie)
            resource->dataForwardingNotPossible = true;

        ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_PDUSessionType);
        if (ie)
            resource->sessionType = ngap_utils::PduSessionTypeFromAsn(ie->PDUSessionType);

        ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_UL_NGU_UP_TNLInformation);
        if (ie)
        {
            resource->upTunnel.teid =
                (uint32_t)asn::GetOctet4(ie->UPTransportLayerInformation.choice.gTPTunnel->gTP_TEID);

            resource->upTunnel.address =
                asn::GetOctetString(ie->UPTransportLayerInformation.choice.gTPTunnel->transportLayerAddress);
        }

        ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_QosFlowSetupRequestList);
        if (ie)
        {
            auto *ptr = asn::New<ASN_NGAP_QosFlowSetupRequestList>();
            asn::DeepCopy(asn_DEF_ASN_NGAP_QosFlowSetupRequestList, ie->QosFlowSetupRequestList, ptr);
            resource->qosFlows = asn::WrapUnique(ptr, asn_DEF_ASN_NGAP_QosFlowSetupRequestList);
        }

        auto setupError = setupPduSessionResource(ue, resource);
        if (setupError.has_value())
        {
            m_logger->warn("UE[%d] HO PDU session[%d]: setup failed cause=%d", ue->ctxId, resource->psi,
                           (int)setupError.value());

            OctetString encodedFail = MakeHoSetupUnsuccessfulTransfer(setupError.value());

            auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
            failed->pDUSessionID = item->pDUSessionID;
            asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
            failedList.push_back(failed);
        }
        else
        {
            OctetString encodedAck = MakeHoAcknowledgeTransfer(*resource);
            if (encodedAck.length() == 0)
            {
                m_logger->warn("UE[%d] HO PDU session %d: failed to encode acknowledge transfer", ue->ctxId,
                               (int)item->pDUSessionID);

                OctetString encodedFail = MakeHoSetupUnsuccessfulTransfer(NgapCause::Protocol_semantic_error);

                auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
                failed->pDUSessionID = item->pDUSessionID;
                asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
                failedList.push_back(failed);
            }
            else
            {
                auto *admitted = asn::New<ASN_NGAP_PDUSessionResourceAdmittedItem>();
                admitted->pDUSessionID = item->pDUSessionID;
                asn::SetOctetString(admitted->handoverRequestAcknowledgeTransfer, encodedAck);
                admittedList.push_back(admitted);

                m_logger->debug("UE[%d] HO PDU session[%d]: admitted teid=%u", ue->ctxId, resource->psi,
                                resource->downTunnel.teid);
            }
        }

        asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
    }

    if (admittedList.empty())
    {
        sendHandoverFailure(NgapCause::Misc_not_enough_user_plane_processing_resources,
                            "all requested PDU sessions failed setup");
        return;
    }

    std::vector<ASN_NGAP_HandoverRequestAcknowledgeIEs *> ackIes;

    auto *admittedIe = asn::New<ASN_NGAP_HandoverRequestAcknowledgeIEs>();
    admittedIe->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceAdmittedList;
    admittedIe->criticality = ASN_NGAP_Criticality_ignore;
    admittedIe->value.present = ASN_NGAP_HandoverRequestAcknowledgeIEs__value_PR_PDUSessionResourceAdmittedList;
    for (auto *item : admittedList)
        asn::SequenceAdd(admittedIe->value.choice.PDUSessionResourceAdmittedList, item);
    ackIes.push_back(admittedIe);

    if (!failedList.empty())
    {
        auto *failedIe = asn::New<ASN_NGAP_HandoverRequestAcknowledgeIEs>();
        failedIe->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListHOAck;
        failedIe->criticality = ASN_NGAP_Criticality_ignore;
        failedIe->value.present =
            ASN_NGAP_HandoverRequestAcknowledgeIEs__value_PR_PDUSessionResourceFailedToSetupListHOAck;
        for (auto *item : failedList)
            asn::SequenceAdd(failedIe->value.choice.PDUSessionResourceFailedToSetupListHOAck, item);
        ackIes.push_back(failedIe);
    }

    OctetString targetToSourceContainer =
        MakeTargetToSourceTransparentContainer(targetRrcContainer, CUSTOM_T2S_DEFAULT_BLOB_SIZE);
    if (targetToSourceContainer.length() == 0)
    {
        sendHandoverFailure(NgapCause::Protocol_semantic_error,
                            "failed to encode TargetToSource transparent container");
        return;
    }

    m_logger->debug("UE[%d] Built TargetToSource wrapper: rrc=%dB total=%dB", ue->ctxId,
                    targetRrcContainer.length(), targetToSourceContainer.length());


    /* Send Handover Request Acknowledge to AMF*/

    auto *t2sIe = asn::New<ASN_NGAP_HandoverRequestAcknowledgeIEs>();
    t2sIe->id = ASN_NGAP_ProtocolIE_ID_id_TargetToSource_TransparentContainer;
    t2sIe->criticality = ASN_NGAP_Criticality_reject;
    t2sIe->value.present =
        ASN_NGAP_HandoverRequestAcknowledgeIEs__value_PR_TargetToSource_TransparentContainer;
    asn::SetOctetString(t2sIe->value.choice.TargetToSource_TransparentContainer, targetToSourceContainer);
    ackIes.push_back(t2sIe);

    auto *ackPdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverRequestAcknowledge>(ackIes);
    sendNgapUeAssociated(ue->ctxId, ackPdu);

    m_logger->info("UE[%d] HandoverRequestAcknowledge sent admitted=%d failed=%d", ue->ctxId,
                   (int)admittedList.size(), (int)failedList.size());
}

/**
 * @brief Source GNB sends a HandoverRequired message to the AMF to trigger handover for the specified UE 
 * to the target PCI. The cause parameter indicates the reason for the handover and is included in the 
 * message to assist the AMF in making informed decisions during the handover process.
 * 
 * Note: in this simulator, the HandoverRequired message includes a custom SourceToTarget-TransparentContainer 
 * that encapsulates the RRC handover preparation information.  This is done to avoid modification of the ASN.1
 * definitions and to simplify the implementation by reusing the existing NGAP message structure. The custom container
 * is identified by a specific magic number in the beginning of the container payload, allowing the target
 * gNB to recognize and decode the RRC handover preparation information.
 * 
 * @param ueId 
 * @param targetPci 
 * @param cause 
 */
void NgapTask::sendHandoverRequired(int ueId, int targetPci, NgapCause cause, bool hoForChoPreparation)
{
    m_logger->info("UE[%d] Sending HandoverRequired to AMF targetPCI=%d", ueId, targetPci);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendHandoverRequired: UE context not found UE[%d] ", ueId);
        return;
    }

    auto neighborOpt = m_base->neighbors->findByPci(targetPci);
    if (!neighborOpt)
    {
        m_logger->err("sendHandoverRequired: target PCI=%d not found in neighborList", targetPci);
        return;
    }
    const auto &neighbor = *neighborOpt;

    if (neighbor.handoverInterface == EHandoverInterface::Xn)
    {
        m_logger->warn("neighborList entry for PCI=%d requests Xn (%s), but only N2 is implemented; "
                       "continuing via N2",
                       targetPci, neighbor.ipAddress.c_str());
    }

    m_logger->info("Resolved target neighbor PCI=%d -> NCGI(plmn=%03d-%02d nci=0x%09llx gnbId=%u cellId=%d) "
                   "tac=%d interface=%s",
                   targetPci,
                   m_base->config->plmn.mcc,
                   m_base->config->plmn.mnc,
                   static_cast<unsigned long long>(neighbor.getNrCellIdentity()),
                   neighbor.getGnbId(),
                   neighbor.getCellId(),
                   neighbor.tac,
                   neighbor.handoverInterface == EHandoverInterface::N2 ? "N2" : "Xn");

    std::vector<ASN_NGAP_HandoverRequiredIEs *> ies;

    // IE: HandoverType = intra5gs
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_HandoverType;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_HandoverType;
        ie->value.choice.HandoverType = ASN_NGAP_HandoverType_intra5gs;
        ies.push_back(ie);
    }

    // IE: Cause
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_Cause;
        ngap_utils::ToCauseAsn_Ref(cause, ie->value.choice.Cause);
        ies.push_back(ie);
    }

    // IE: TargetID — resolve target gNB from NRT by PCI and derive NCGI fields.
    // PLMN is assumed to be the same as the serving gNB config.
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_TargetID;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_TargetID;

        ie->value.choice.TargetID.present = ASN_NGAP_TargetID_PR_targetRANNodeID;
        auto *targetRanNode = asn::New<ASN_NGAP_TargetRANNodeID>();

        // GlobalRANNodeID → GlobalGNB-ID
        auto *globalGnbId = asn::New<ASN_NGAP_GlobalGNB_ID>();
        asn::SetOctetString3(globalGnbId->pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        globalGnbId->gNB_ID.present = ASN_NGAP_GNB_ID_PR_gNB_ID;

        auto gnbIdLength = neighbor.idLength;
        auto bitsToShift = 32 - gnbIdLength;

        asn::SetBitString(globalGnbId->gNB_ID.choice.gNB_ID,
                  octet4{neighbor.getGnbId() << bitsToShift},
                          static_cast<size_t>(gnbIdLength));

        targetRanNode->globalRANNodeID.present = ASN_NGAP_GlobalRANNodeID_PR_globalGNB_ID;
        targetRanNode->globalRANNodeID.choice.globalGNB_ID = globalGnbId;

        // selectedTAI
        asn::SetOctetString3(targetRanNode->selectedTAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(targetRanNode->selectedTAI.tAC, octet3{neighbor.tac});

        ie->value.choice.TargetID.choice.targetRANNodeID = targetRanNode;
        ies.push_back(ie);
    }

    // IE: SourceToTarget-TransparentContainer
    // Populate a standards-shaped RRC HandoverPreparationInformation payload.
    {
        int sourcePci = cons::getPciFromNci(m_base->config->nci);
        HandoverPreparationInfo handoverPrep{};
        handoverPrep.measIdentities = m_base->rrcTask->getHandoverMeasurementIdentities(ueId);
        handoverPrep.measConfigRrcReconfiguration =
            m_base->rrcTask->getHandoverMeasConfigRrcReconfiguration(ueId);

        OctetString sourceToTarget = MakeSourceToTargetTransparentContainer(*ue,
                                                                            ueId,
                                                                            sourcePci,
                                                                            CUSTOM_S2T_DEFAULT_BLOB_SIZE,
                                                                            handoverPrep,
                                                                            hoForChoPreparation);
        if (sourceToTarget.length() == 0)
        {
            m_logger->err("sendHandoverRequired: failed to encode SourceToTarget transparent container");
            return;
        }

        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present =
            ASN_NGAP_HandoverRequiredIEs__value_PR_SourceToTarget_TransparentContainer;
        asn::SetOctetString(ie->value.choice.SourceToTarget_TransparentContainer,
                            sourceToTarget);
        ies.push_back(ie);

        m_logger->info("UE[%d] Encoded SourceToTarget container sourcePCI=%d targetPCI=%d size=%dB cho=%d",
                       ueId,
                       sourcePci,
                       targetPci,
                   sourceToTarget.length(),
                   hoForChoPreparation ? 1 : 0);
    }

    // IE: PDUSessionResourceListHORqd
    {
        OctetString hoRequiredTransfer = MakeHoRequiredTransfer();
        if (hoRequiredTransfer.length() == 0)
        {
            m_logger->err("sendHandoverRequired: failed to encode HandoverRequiredTransfer");
            return;
        }

        std::vector<int> pduSessionIds{};
        if (!ue->pduSessions.empty())
        {
            pduSessionIds.assign(ue->pduSessions.begin(), ue->pduSessions.end());
        }
        else
        {
            // Simulation fallback: allow N2 handover signaling to proceed before user-plane setup.
            pduSessionIds.push_back(1);
            m_logger->warn("sendHandoverRequired: UE[%d] has no active PDU sessions; using synthetic session id=1",
                           ueId);
        }

        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceListHORqd;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_PDUSessionResourceListHORqd;

        for (int psi : pduSessionIds)
        {
            auto *item = asn::New<ASN_NGAP_PDUSessionResourceItemHORqd>();
            item->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
            asn::SetOctetString(item->handoverRequiredTransfer, hoRequiredTransfer);
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceListHORqd, item);
        }

        ies.push_back(ie);

        m_logger->info("HandoverRequired UE[%d] includes %d PDU session item(s)",
                       ueId,
                       static_cast<int>(pduSessionIds.size()));
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverRequired>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    // Track pending CHO preparation by target PCI, so responses can be correlated per target.
    if (hoForChoPreparation)
        m_hoReqChoPendingByTargetPci[ueId][targetPci]++;

    m_logger->info("UE[%d] HandoverRequired sent to AMF (mode=%s)",
                   ueId,
                   hoForChoPreparation ? "cho-prepare" : "classic");
}

/**
 * @brief AMF sends a HandoverCommand to the source gNB to instruct it to proceed with the handover 
 * for the specified UE.  The HandoverCommand includes a TargetToSource-TransparentContainer that 
 * carries the RRC container from the Target gNB, which contains necessary information for the 
 * UE to connect to the target gNB (e.g., the C-RNTI).
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveHandoverCommand(int amfId, ASN_NGAP_HandoverCommand *msg)
{
    m_logger->info("HandoverCommand received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receiveHandoverCommand: UE not found");
        return;
    }

    // Extract TargetToSource-TransparentContainer (contains the RRC container)
    auto *ieContainer = asn::ngap::GetProtocolIe(msg,
        ASN_NGAP_ProtocolIE_ID_id_TargetToSource_TransparentContainer);

    OctetString rrcContainer{};
    if (ieContainer)
    {
        OctetString encodedT2s = asn::GetOctetString(ieContainer->TargetToSource_TransparentContainer);
        m_logger->debug("Received TargetToSource wrapper UE[%d] from AMF: total=%dB", ue->ctxId,
                        encodedT2s.length());

        auto decodedRrc = DecodeTargetToSourceTransparentContainer(encodedT2s);
        if (!decodedRrc.has_value())
        {
            m_logger->err("receiveHandoverCommand: invalid TargetToSource transparent container");
            sendErrorIndication(amfId, NgapCause::Protocol_transfer_syntax_error);
            return;
        }

        rrcContainer = std::move(decodedRrc.value());
        m_logger->debug("UE[%d] Decoded TargetToSource wrapper rrcContainer=%dB", ue->ctxId,
                        rrcContainer.length());
    }

    int targetPci = -1;
    bool targetPciExtracted = ExtractTargetPciFromHandoverCommandRrcContainer(rrcContainer, targetPci);
    bool hoForChoPreparation = false;
    
    if (!targetPciExtracted)
    {
        m_logger->warn("UE[%d] Could not extract targetPCI from HO command container; defaulting mode=%s",
                       ue->ctxId,
                       hoForChoPreparation ? "cho-prepare" : "classic");
    }
    else 
    {

        // check to see if this is a response to a CHO preparation we initiated for this UE and target PCI; 
        // if so, mark it so RRC can handle accordingly, and remove from the UE's pending cho list.

        auto itByUe = m_hoReqChoPendingByTargetPci.find(ue->ctxId);
        if (itByUe != m_hoReqChoPendingByTargetPci.end())
        {
            auto &targetMap = itByUe->second;
            auto itTarget = targetMap.find(targetPci);
            if (itTarget != targetMap.end() && itTarget->second > 0)
            {
                hoForChoPreparation = true;
                itTarget->second--;
                if (itTarget->second <= 0)
                    targetMap.erase(itTarget);
                if (targetMap.empty())
                    m_hoReqChoPendingByTargetPci.erase(itByUe);
            }
        }
    }

    // Forward to RRC task

    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_COMMAND_DELIVERY);
    w->ueId = ue->ctxId;
    w->rrcContainer = std::move(rrcContainer);
    w->hoTargetPci = targetPci;
    w->hoForChoPreparation = hoForChoPreparation;
    m_base->rrcTask->push(std::move(w));

    m_logger->info("UE[%d] HandoverCommand forwarded to RRC rrcContainer=%dB targetPCI=%d mode=%s",
                   ue->ctxId,
                   rrcContainer.length(),
                   targetPci,
                   hoForChoPreparation ? "cho-prepare" : "classic");
}

/**
 * @brief AMF sends a HandoverPreparationFailure to the source gNB to indicate that the 
 * handover preparation has failed.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveHandoverPreparationFailure(int amfId, ASN_NGAP_HandoverPreparationFailure *msg)
{
    // FIX - need to figure out how to determine which targetPCI this message came from
    int hoTargetPci = -1;
    m_logger->warn("HandoverPreparationFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));

    if (!ue)
    {
        m_logger->err("receiveHandoverPreparationFailure: UE not found");
        return;
    }

    // remove from pending CHO list if present, so that subsequent handover attempts to the same target can be properly correlated.
    
    auto itByUe = m_hoReqChoPendingByTargetPci.find(ue->ctxId);
    if (itByUe != m_hoReqChoPendingByTargetPci.end())
    {
        // targetPCIs are tracked as a map in the second value
        auto &targetMap = itByUe->second;
        // if the targetPCI is in the map, remove it
        if (targetMap.count(hoTargetPci) != 0)
        {
            targetMap.erase(hoTargetPci);

            // if the map is now empty, remove the UE entry as well
            if (targetMap.empty())
                m_hoReqChoPendingByTargetPci.erase(itByUe);
        }
        else
        {
            m_logger->warn("UE[%d] HO prep failure cannot be target-correlated; keeping %zu pending target(s)",
                           ue->ctxId,
                           targetMap.size());
        }
    }

    // Extract cause for logging
    auto *ieCause = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_Cause);
    if (ieCause)
    {
        m_logger->warn("UE[%d] Handover preparation failed cause present=%d",
                       ue->ctxId, ieCause->Cause.present);
    }

    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_FAILURE);
    w->ueId = ue->ctxId;
    w->hoTargetPci = hoTargetPci;
    m_base->rrcTask->push(std::move(w));

    m_logger->warn("UE[%d] Handover preparation failed. UE remains on source cell.", ue->ctxId);
}

/**
 * @brief target GNB sends a HandoverNotify to the AMF to indicate that the handover has 
 * been completed and the UE is now being served by the target gNB. This allows the AMF 
 * to update its context for the UE and notify the source gNB to release resources associated with the UE. 
 * The HandoverNotify includes UserLocationInformation to provide the AMF with the UE's 
 * new location after the handover, which can be used for various purposes such as 
 * location-based services, mobility management, and optimizing network resources.
 * 
 * @param ueId 
 */
void NgapTask::sendHandoverNotify(int ueId)
{

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendHandoverNotify: UE context not found UE[%d] ", ueId);
        return;
    }

    std::vector<ASN_NGAP_HandoverNotifyIEs *> ies;

    // IE: UserLocationInformation (required)
    {
        auto *ie = asn::New<ASN_NGAP_HandoverNotifyIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverNotifyIEs__value_PR_UserLocationInformation;

        ie->value.choice.UserLocationInformation.present =
            ASN_NGAP_UserLocationInformation_PR_userLocationInformationNR;
        auto *nrLoc = asn::New<ASN_NGAP_UserLocationInformationNR>();

        // NR-CGI
        asn::SetOctetString3(nrLoc->nR_CGI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetBitStringLong<36>(m_base->config->nci, nrLoc->nR_CGI.nRCellIdentity);

        // TAI
        asn::SetOctetString3(nrLoc->tAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(nrLoc->tAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.UserLocationInformation.choice.userLocationInformationNR = nrLoc;
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverNotify>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("UE[%d] HandoverNotify sent to AMF", ueId);
}

/**
 * @brief Target GNB sends a PathSwitchRequest to the AMF to request switching the UE's path 
 * to the new target gNB after handover completion.  Only used in Xn handover, and must NOT 
 * be sent in N2 handover as the path switch is implicitly triggered by the HandoverNotify.
 * 
 * @param ueId 
 */
void NgapTask::sendPathSwitchRequest(int ueId)
{
    m_logger->info("UE[%d] Sending PathSwitchRequest", ueId);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendPathSwitchRequest: UE context not found UE[%d] ", ueId);
        return;
    }

    std::vector<ASN_NGAP_PathSwitchRequestIEs *> ies;

    // IE: UserLocationInformation
    {
        auto *ie = asn::New<ASN_NGAP_PathSwitchRequestIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_PathSwitchRequestIEs__value_PR_UserLocationInformation;

        ie->value.choice.UserLocationInformation.present =
            ASN_NGAP_UserLocationInformation_PR_userLocationInformationNR;
        auto *nrLoc = asn::New<ASN_NGAP_UserLocationInformationNR>();

        asn::SetOctetString3(nrLoc->nR_CGI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetBitStringLong<36>(m_base->config->nci, nrLoc->nR_CGI.nRCellIdentity);
        asn::SetOctetString3(nrLoc->tAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(nrLoc->tAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.UserLocationInformation.choice.userLocationInformationNR = nrLoc;
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_PathSwitchRequest>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("UE[%d] PathSwitchRequest sent to AMF", ueId);
}

/**
 * @brief Target GNB receives the PathSwitchRequestAcknowledge from the AMF to indicate that 
 * the path switch has been completed.  Only used for Xn handover.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receivePathSwitchRequestAcknowledge(int amfId, ASN_NGAP_PathSwitchRequestAcknowledge *msg)
{
    m_logger->info("PathSwitchRequestAcknowledge received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestAcknowledge: UE not found");
        return;
    }

    m_logger->info("UE[%d] Path switch complete. AMF path updated.", ue->ctxId);

    // Notify RRC that path switch is acknowledged
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK);
    w->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w));

    if (m_base->xnTask)
    {
        auto x = std::make_unique<NmGnbNgapToXn>(NmGnbNgapToXn::PATH_SWITCH_ACK);
        x->ueId = ue->ctxId;
        x->success = true;
        m_base->xnTask->push(std::move(x));
    }
}

/**
 * @brief Target GNB receives the PathSwitchRequestFailure from the AMF to indicate that 
 * the path switch has failed.  Only used for Xn handover. The target gNB should keep the 
 * UE on the source cell and may choose to retry the path switch or trigger a new handover 
 * if necessary.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receivePathSwitchRequestFailure(int amfId, ASN_NGAP_PathSwitchRequestFailure *msg)
{
    m_logger->warn("PathSwitchRequestFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestFailure: UE not found");
        return;
    }

    m_logger->warn("UE[%d] Path switch failed.", ue->ctxId);

    if (m_base->xnTask)
    {
        auto x = std::make_unique<NmGnbNgapToXn>(NmGnbNgapToXn::PATH_SWITCH_ACK);
        x->ueId = ue->ctxId;
        x->success = false;
        m_base->xnTask->push(std::move(x));
    }
}

/**
 * @brief Handles handover notify messages received from the RRC task. Activates the context from the handover pending map.
 * Send the HandoverNotify message to AMF to indicate handover completion. In N2 handover, this is the final step of the 
 * handover procedure, and the AMF will then send UEContextReleaseCommand to the source gNB to free its context. 
 * In Xn handover, the target gNB must also send a PathSwitchRequest to the AMF to request switching the UE's path to the 
 * new target gNB after handover completion.
 * 
 * @param ueId 
 */

void NgapTask::handleHandoverNotifyFromRrc(int ueId)
{
    // move the UE context from handover pending map to ueCtx map
    auto it = m_handoverPending.find(ueId);
    if (it == m_handoverPending.end() || it->second == nullptr || it->second->ctx == nullptr)
    {
        m_logger->warn("UE[%d] Ignoring handover complete: no pending NGAP handover context", ueId);
        return;
    }

    m_logger->info("UE[%d] Processing handover complete, sending HandoverNotify", ueId);

    auto *hoPending = it->second;
    m_ueCtx[ueId] = hoPending->ctx;
    delete hoPending;
    m_handoverPending.erase(it);

    // Send HandoverNotify to AMF to indicate handover completion and update UE location.
    sendHandoverNotify(ueId);
}

} // namespace nr::gnb
