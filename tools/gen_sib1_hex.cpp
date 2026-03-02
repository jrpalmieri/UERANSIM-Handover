// Small utility to generate valid UPER-encoded MIB and SIB1 bytes
// for the test harness fallback constants.

#include <cstdio>
#include <lib/asn/rrc.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common_types.hpp>
#include <utils/bits.hpp>

#include <asn/rrc/ASN_RRC_BCCH-BCH-Message.h>
#include <asn/rrc/ASN_RRC_BCCH-DL-SCH-Message.h>
#include <asn/rrc/ASN_RRC_MIB.h>
#include <asn/rrc/ASN_RRC_SIB1.h>
#include <asn/rrc/ASN_RRC_PLMN-IdentityInfo.h>
#include <asn/rrc/ASN_RRC_UAC-BarringInfoSet.h>
#include <asn/rrc/ASN_RRC_UAC-BarringPerCat.h>
#include <asn/rrc/ASN_RRC_DL-CCCH-Message.h>
#include <asn/rrc/ASN_RRC_RRCSetup.h>
#include <asn/rrc/ASN_RRC_RRCSetup-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_RRCRelease.h>
#include <asn/rrc/ASN_RRC_RRCRelease-IEs.h>

static void printHex(const char *label, const OctetString &data)
{
    printf("%s (%d bytes): ", label, data.length());
    for (int i = 0; i < data.length(); i++)
        printf("%02x", (unsigned char)data.data()[i]);
    printf("\n");
}

static OctetString encodeMib()
{
    auto *pdu = asn::New<ASN_RRC_BCCH_BCH_Message>();
    pdu->message.present = ASN_RRC_BCCH_BCH_MessageType_PR_mib;
    pdu->message.choice.mib = asn::New<ASN_RRC_MIB>();
    auto &mib = *pdu->message.choice.mib;

    asn::SetBitStringInt<6>(0, mib.systemFrameNumber); // SFN = 0
    mib.subCarrierSpacingCommon = ASN_RRC_MIB__subCarrierSpacingCommon_scs15or60;
    mib.ssb_SubcarrierOffset = 0;
    mib.dmrs_TypeA_Position = ASN_RRC_MIB__dmrs_TypeA_Position_pos2;
    mib.pdcch_ConfigSIB1.controlResourceSetZero = 0;
    mib.pdcch_ConfigSIB1.searchSpaceZero = 0;
    mib.cellBarred = ASN_RRC_MIB__cellBarred_notBarred;
    mib.intraFreqReselection = ASN_RRC_MIB__intraFreqReselection_allowed;
    asn::SetBitStringInt<1>(0, mib.spare);

    auto result = rrc::encode::EncodeS(asn_DEF_ASN_RRC_BCCH_BCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_BCCH_BCH_Message, pdu);
    return result;
}

static OctetString encodeSib1(int mcc, int mnc, int tac, int64_t nci)
{
    auto *pdu = asn::New<ASN_RRC_BCCH_DL_SCH_Message>();
    pdu->message.present = ASN_RRC_BCCH_DL_SCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1;
    pdu->message.choice.c1->choice.systemInformationBlockType1 = asn::New<ASN_RRC_SIB1>();

    auto &sib1 = *pdu->message.choice.c1->choice.systemInformationBlockType1;

    // Cell access related info
    Plmn plmn;
    plmn.mcc = mcc;
    plmn.mnc = mnc;
    plmn.isLongMnc = (mnc > 99);

    auto *plmnInfo = asn::New<ASN_RRC_PLMN_IdentityInfo>();
    plmnInfo->cellReservedForOperatorUse = ASN_RRC_PLMN_IdentityInfo__cellReservedForOperatorUse_notReserved;
    asn::MakeNew(plmnInfo->trackingAreaCode);
    asn::SetBitStringInt<24>(tac, *plmnInfo->trackingAreaCode);
    asn::SetBitStringLong<36>(nci, plmnInfo->cellIdentity);
    asn::SequenceAdd(plmnInfo->plmn_IdentityList, asn::rrc::NewPlmnId(plmn));
    asn::SequenceAdd(sib1.cellAccessRelatedInfo.plmn_IdentityList, plmnInfo);

    auto result = rrc::encode::EncodeS(asn_DEF_ASN_RRC_BCCH_DL_SCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_BCCH_DL_SCH_Message, pdu);
    return result;
}

static OctetString encodeRrcSetup(int transactionId)
{
    auto *pdu = asn::New<ASN_RRC_DL_CCCH_Message>();
    pdu->message.present = ASN_RRC_DL_CCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_CCCH_MessageType__c1_PR_rrcSetup;

    auto *setup = asn::New<ASN_RRC_RRCSetup>();
    setup->rrc_TransactionIdentifier = transactionId;
    setup->criticalExtensions.present = ASN_RRC_RRCSetup__criticalExtensions_PR_rrcSetup;
    setup->criticalExtensions.choice.rrcSetup = asn::New<ASN_RRC_RRCSetup_IEs>();

    // masterCellGroup — encode an empty CellGroupConfig
    auto *mcg = asn::New<ASN_RRC_CellGroupConfig>();
    mcg->cellGroupId = 0;
    OctetString mcgEnc = rrc::encode::EncodeS(asn_DEF_ASN_RRC_CellGroupConfig, mcg);
    asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, mcg);
    asn::SetOctetString(setup->criticalExtensions.choice.rrcSetup->masterCellGroup, mcgEnc);

    pdu->message.choice.c1->choice.rrcSetup = setup;

    auto result = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_CCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_CCCH_Message, pdu);
    return result;
}

static OctetString encodeDlInfoTransfer(int transactionId, const uint8_t *nasPdu, int nasLen)
{
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_dlInformationTransfer;
    pdu->message.choice.c1->choice.dlInformationTransfer = asn::New<ASN_RRC_DLInformationTransfer>();

    auto *dit = pdu->message.choice.c1->choice.dlInformationTransfer;
    dit->rrc_TransactionIdentifier = transactionId;
    dit->criticalExtensions.present =
        ASN_RRC_DLInformationTransfer__criticalExtensions_PR_dlInformationTransfer;
    dit->criticalExtensions.choice.dlInformationTransfer =
        asn::New<ASN_RRC_DLInformationTransfer_IEs>();
    dit->criticalExtensions.choice.dlInformationTransfer->dedicatedNAS_Message =
        asn::New<ASN_RRC_DedicatedNAS_Message_t>();

    OctetString nasBuf;
    for (int i = 0; i < nasLen; i++)
        nasBuf.appendOctet(nasPdu[i]);
    asn::SetOctetString(
        *dit->criticalExtensions.choice.dlInformationTransfer->dedicatedNAS_Message,
        nasBuf);

    auto result = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    return result;
}

static OctetString encodeRrcRelease(int transactionId)
{
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcRelease;
    pdu->message.choice.c1->choice.rrcRelease = asn::New<ASN_RRC_RRCRelease>();

    auto *rel = pdu->message.choice.c1->choice.rrcRelease;
    rel->rrc_TransactionIdentifier = transactionId;
    rel->criticalExtensions.present =
        ASN_RRC_RRCRelease__criticalExtensions_PR_rrcRelease;
    rel->criticalExtensions.choice.rrcRelease = asn::New<ASN_RRC_RRCRelease_IEs>();

    auto result = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    return result;
}

int main()
{
    auto mib = encodeMib();
    printHex("MIB", mib);

    // Default params for test harness: MCC=286, MNC=93, TAC=1, NCI=1
    auto sib1 = encodeSib1(286, 93, 1, 1);
    printHex("SIB1(286/93/1/1)", sib1);

    auto rrcSetup = encodeRrcSetup(0);
    printHex("RRCSetup(tid=0)", rrcSetup);

    // DLInformationTransfer with various NAS sizes
    uint8_t nas1[] = {0xAA}; // 1 byte
    auto d1 = encodeDlInfoTransfer(0, nas1, 1);
    printHex("DLInfoTransfer(tid=0, 1B)", d1);

    uint8_t nas3[] = {0xAA, 0xBB, 0xCC}; // 3 bytes
    auto d3 = encodeDlInfoTransfer(0, nas3, 3);
    printHex("DLInfoTransfer(tid=0, 3B)", d3);

    uint8_t nas4[] = {0xAA, 0xBB, 0xCC, 0xDD}; // 4 bytes
    auto d4 = encodeDlInfoTransfer(0, nas4, 4);
    printHex("DLInfoTransfer(tid=0, 4B)", d4);

    // tid=1
    auto d1t1 = encodeDlInfoTransfer(1, nas1, 1);
    printHex("DLInfoTransfer(tid=1, 1B)", d1t1);

    // tid=2
    auto d1t2 = encodeDlInfoTransfer(2, nas1, 1);
    printHex("DLInfoTransfer(tid=2, 1B)", d1t2);

    auto rrcRelease = encodeRrcRelease(0);
    printHex("RRCRelease(tid=0)", rrcRelease);

    return 0;
}
