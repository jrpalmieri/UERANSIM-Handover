#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/octet_string.hpp>

#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_EventTriggerConfig.h>
#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasObjectNR.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddModList.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_ReportConfigNR.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddMod.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddModList.h>
#include <asn/rrc/ASN_RRC_SSB-MTC.h>

namespace
{

const char *decodeCodeName(asn_dec_rval_code_e code)
{
    switch (code)
    {
    case RC_OK:
        return "RC_OK";
    case RC_WMORE:
        return "RC_WMORE";
    case RC_FAIL:
        return "RC_FAIL";
    default:
        return "RC_UNKNOWN";
    }
}

void printPdu(const std::string &name, const OctetString &pdu)
{
    std::cout << name << " len=" << pdu.length() << " hex=" << pdu.toHexString() << "\n";
}

OctetString encodeWithDiag(const std::string &label, ASN_RRC_DL_DCCH_Message *pdu)
{
    auto res = asn_encode_to_new_buffer(nullptr, ATS_UNALIGNED_CANONICAL_PER,
                                        &asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    if (res.buffer == nullptr || res.result.encoded < 0)
    {
        const char *failedType =
            (res.result.failed_type && res.result.failed_type->name) ? res.result.failed_type->name : "<unknown>";
        std::cout << label << " encode failed: encoded=" << res.result.encoded
                  << " failedType=" << failedType << "\n";
        return OctetString{};
    }

    std::vector<uint8_t> out(static_cast<size_t>(res.result.encoded));
    memcpy(out.data(), res.buffer, static_cast<size_t>(res.result.encoded));
    free(res.buffer);
    return OctetString{std::move(out)};
}

void checkConstraints(const std::string &label, asn_TYPE_descriptor_t &desc, const void *sptr)
{
    char errbuf[512]{};
    size_t errlen = sizeof(errbuf);
    int rc = asn_check_constraints(&desc, sptr, errbuf, &errlen);
    if (rc == 0)
    {
        std::cout << label << " constraints: OK\n";
        return;
    }

    std::cout << label << " constraints: FAIL " << errbuf << "\n";
}

void tryEncodeType(const std::string &label, asn_TYPE_descriptor_t &desc, void *sptr)
{
    auto res = asn_encode_to_new_buffer(nullptr, ATS_UNALIGNED_CANONICAL_PER, &desc, sptr);
    if (res.buffer == nullptr || res.result.encoded < 0)
    {
        const char *failedType =
            (res.result.failed_type && res.result.failed_type->name) ? res.result.failed_type->name : "<unknown>";
        std::cout << label << " encode: FAIL encoded=" << res.result.encoded
                  << " failedType=" << failedType << "\n";
        return;
    }

    std::cout << label << " encode: OK bits=" << res.result.encoded << "\n";
    free(res.buffer);
}

ASN_RRC_RRCReconfiguration_IEs *newReconfigIes(ASN_RRC_DL_DCCH_Message &pdu)
{
    pdu.message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu.message.choice.c1 = asn::NewFor(pdu.message.choice.c1);
    pdu.message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto *reconfig = asn::New<ASN_RRC_RRCReconfiguration>();
    pdu.message.choice.c1->choice.rrcReconfiguration = reconfig;
    reconfig->rrc_TransactionIdentifier = 0;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    reconfig->criticalExtensions.choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration_IEs>();
    auto *ies = reconfig->criticalExtensions.choice.rrcReconfiguration;

    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    auto *mc = ies->measConfig;

    mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
    measObj->measObjectId = 1;
    measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
    measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
    measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628;
    measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz15;
    measObj->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
        ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
    measObj->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
    measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
    asn::SequenceAdd(*mc->measObjectToAddModList, measObj);

    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();

    auto *measId = asn::New<ASN_RRC_MeasIdToAddMod>();
    measId->measId = 1;
    measId->measObjectId = 1;
    measId->reportConfigId = 1;
    asn::SequenceAdd(*mc->measIdToAddModList, measId);

    return ies;
}

OctetString buildA3Pdu()
{
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    auto *ies = newReconfigIes(*pdu);

    auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
    rcMod->reportConfigId = 1;
    rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
    rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

    auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
    rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
    rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
    auto *et = rcNR->reportType.choice.eventTriggered;

    et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
    asn::MakeNew(et->eventId.choice.eventA3);
    auto *a3 = et->eventId.choice.eventA3;
    a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
    a3->a3_Offset.choice.rsrp = 6;
    a3->reportOnLeave = false;
    a3->hysteresis = 2;
    a3->timeToTrigger = ASN_RRC_TimeToTrigger_ms160;
    a3->useAllowedCellList = false;

    et->rsType = ASN_RRC_NR_RS_Type_ssb;
    et->reportInterval = ASN_RRC_ReportInterval_ms1024;
    et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
    et->reportQuantityCell.rsrp = true;
    et->reportQuantityCell.rsrq = false;
    et->reportQuantityCell.sinr = false;
    et->maxReportCells = 8;
    et->includeBeamMeasurements = false;

    asn::SequenceAdd(*ies->measConfig->reportConfigToAddModList, rcMod);

    OctetString encoded = encodeWithDiag("C++ A3", pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    return encoded;
}

OctetString buildD1Pdu(bool useFixedReference)
{
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    auto *ies = newReconfigIes(*pdu);

    auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
    rcMod->reportConfigId = 1;
    rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
    rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

    auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
    rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
    rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
    auto *et = rcNR->reportType.choice.eventTriggered;

    et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17;
    asn::MakeNew(et->eventId.choice.eventD1_r17);
    auto *d1 = et->eventId.choice.eventD1_r17;
    d1->distanceThreshFromReference1_r17 = 5000;
    d1->distanceThreshFromReference2_r17 = 5000;
    d1->reportOnLeave_r17 = false;
    d1->hysteresisLocation_r17 = 0;
    d1->timeToTrigger = ASN_RRC_TimeToTrigger_ms160;

    if (useFixedReference)
    {
        nr::rrc::common::EventReferenceLocation ref1{};
        ref1.latitudeDeg = 33.77;
        ref1.longitudeDeg = -84.39;
        nr::rrc::common::referenceLocationToAsnValue(ref1, d1->referenceLocation1_r17);

        nr::rrc::common::EventReferenceLocation ref2{};
        ref2.latitudeDeg = 34.05;
        ref2.longitudeDeg = -118.24;
        nr::rrc::common::referenceLocationToAsnValue(ref2, d1->referenceLocation2_r17);
    }
    else
    {
        nr::rrc::common::EventReferenceLocation ref1{};
        ref1.latitudeDeg = 0.0;
        ref1.longitudeDeg = 0.0;
        nr::rrc::common::referenceLocationToAsnValue(ref1, d1->referenceLocation1_r17);

        nr::rrc::common::EventReferenceLocation ref2{};
        ref2.latitudeDeg = 0.0;
        ref2.longitudeDeg = 0.0;
        nr::rrc::common::referenceLocationToAsnValue(ref2, d1->referenceLocation2_r17);
    }

    et->rsType = ASN_RRC_NR_RS_Type_ssb;
    et->reportInterval = ASN_RRC_ReportInterval_ms1024;
    et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
    et->reportQuantityCell.rsrp = true;
    et->reportQuantityCell.rsrq = false;
    et->reportQuantityCell.sinr = false;
    et->maxReportCells = 8;
    et->includeBeamMeasurements = false;

    checkConstraints("C++ D1 EventTriggerConfig", asn_DEF_ASN_RRC_EventTriggerConfig, et);
    checkConstraints("C++ D1 ReportConfigToAddMod", asn_DEF_ASN_RRC_ReportConfigToAddMod, rcMod);
    tryEncodeType("C++ D1 EventTriggerConfig", asn_DEF_ASN_RRC_EventTriggerConfig, et);
    tryEncodeType("C++ D1 ReportConfigNR", asn_DEF_ASN_RRC_ReportConfigNR, rcNR);
    tryEncodeType("C++ D1 ReportConfigToAddMod", asn_DEF_ASN_RRC_ReportConfigToAddMod, rcMod);

    asn::SequenceAdd(*ies->measConfig->reportConfigToAddModList, rcMod);
    tryEncodeType("C++ D1 ReportConfigToAddModList", asn_DEF_ASN_RRC_ReportConfigToAddModList,
                  ies->measConfig->reportConfigToAddModList);
    tryEncodeType("C++ D1 MeasConfig", asn_DEF_ASN_RRC_MeasConfig, ies->measConfig);
    tryEncodeType("C++ D1 RRCReconfiguration-IEs", asn_DEF_ASN_RRC_RRCReconfiguration_IEs, ies);

    const std::string label = useFixedReference ? "C++ D1 fixed" : "C++ D1 nadir";
    OctetString encoded = encodeWithDiag(label, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    return encoded;
}

std::string eventName(long present)
{
    switch (present)
    {
    case ASN_RRC_EventTriggerConfig__eventId_PR_eventA3:
        return "eventA3";
    case ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17:
        return "eventD1-r17";
    default:
        return "other(" + std::to_string(present) + ")";
    }
}

void decodeAndPrint(const std::string &label, const OctetString &pdu)
{
    asn_dec_rval_t res{};
    auto *msg = rrc::encode::DecodeWithResult<ASN_RRC_DL_DCCH_Message>(
        asn_DEF_ASN_RRC_DL_DCCH_Message,
        pdu.data(),
        static_cast<size_t>(pdu.length()),
        res);

    std::cout << label << " decode code=" << decodeCodeName(res.code)
              << "(" << static_cast<int>(res.code) << ")"
              << " consumed=" << res.consumed << "\n";

    if (msg == nullptr)
        return;

    if (msg->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
        msg->message.choice.c1 &&
        msg->message.choice.c1->present == ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration)
    {
        auto *reconfig = msg->message.choice.c1->choice.rrcReconfiguration;
        auto *ies = reconfig ? reconfig->criticalExtensions.choice.rrcReconfiguration : nullptr;
        auto *mc = ies ? ies->measConfig : nullptr;
        auto *list = mc ? mc->reportConfigToAddModList : nullptr;
        if (list && list->list.count > 0)
        {
            auto *item = list->list.array[0];
            if (item && item->reportConfig.present == ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR)
            {
                auto *rcNR = item->reportConfig.choice.reportConfigNR;
                if (rcNR && rcNR->reportType.present == ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered)
                {
                    auto *et = rcNR->reportType.choice.eventTriggered;
                    if (et)
                        std::cout << label << " first-event=" << eventName(et->eventId.present) << "\n";
                }
            }
        }
    }

    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, msg);
}

} // namespace

int main(int argc, char **argv)
{
    // Python-generated D1 PDU from test harness (transaction_id=60).
    std::string pythonD1Hex = "0021500003800134e6810000000084006138816cb9241d82524051403104e0000000";
    if (argc > 1)
        pythonD1Hex = argv[1];

    OctetString a3 = buildA3Pdu();
    OctetString d1Fixed = buildD1Pdu(true);
    OctetString d1Nadir = buildD1Pdu(false);
    OctetString pyD1 = OctetString::FromHex(pythonD1Hex);

    printPdu("C++ A3", a3);
    printPdu("C++ D1 fixed", d1Fixed);
    printPdu("C++ D1 nadir", d1Nadir);
    printPdu("Python D1", pyD1);

    decodeAndPrint("C++ A3", a3);
    decodeAndPrint("C++ D1 fixed", d1Fixed);
    decodeAndPrint("C++ D1 nadir", d1Nadir);
    decodeAndPrint("Python D1", pyD1);

    return 0;
}
