#include <asn/rrc/ASN_RRC_CondTriggerConfig-r16.h>
#include <asn/rrc/ASN_RRC_EventTriggerConfig.h>

#include <lib/asn/utils.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <lib/rrc/common/event_types.hpp>
#include <lib/rrc/encode.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void assertNear(double actual, double expected, double tolerance, const std::string &message)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        throw std::runtime_error(message + " expected=" + std::to_string(expected) + " actual=" +
                                 std::to_string(actual) + " tolerance=" + std::to_string(tolerance));
    }
}

void assertEq(long actual, long expected, const std::string &message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message + " expected=" + std::to_string(expected) + " actual=" +
                                 std::to_string(actual));
    }
}

void testEventD1ReferenceLocationRoundTrip()
{
    using nr::rrc::common::EventReferenceLocation;

    auto *event = asn::New<ASN_RRC_EventTriggerConfig>();
    event->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17;
    asn::MakeNew(event->eventId.choice.eventD1_r17);

    auto *d1 = event->eventId.choice.eventD1_r17;
    d1->distanceThreshFromReference1_r17 = 1111;
    d1->distanceThreshFromReference2_r17 = 2222;
    d1->reportOnLeave_r17 = true;
    d1->hysteresisLocation_r17 = 12;
    d1->timeToTrigger = ASN_RRC_TimeToTrigger_ms160;

    EventReferenceLocation loc1{};
    loc1.latitudeDeg = 33.7756;
    loc1.longitudeDeg = -84.3963;
    nr::rrc::common::referenceLocationToAsnValue(loc1, d1->referenceLocation1_r17);

    EventReferenceLocation loc2{};
    loc2.latitudeDeg = -12.045;
    loc2.longitudeDeg = 45.1234;
    nr::rrc::common::referenceLocationToAsnValue(loc2, d1->referenceLocation2_r17);

    event->rsType = ASN_RRC_NR_RS_Type_ssb;
    event->reportInterval = ASN_RRC_ReportInterval_ms480;
    event->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
    event->reportQuantityCell.rsrp = true;
    event->reportQuantityCell.rsrq = false;
    event->reportQuantityCell.sinr = false;
    event->maxReportCells = 4;
    event->includeBeamMeasurements = false;

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_EventTriggerConfig, event);
    if (encoded.length() == 0)
    {
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, event);
        throw std::runtime_error("EventD1 encoding failed");
    }

    auto *decoded = rrc::encode::Decode<ASN_RRC_EventTriggerConfig>(asn_DEF_ASN_RRC_EventTriggerConfig, encoded);
    if (decoded == nullptr)
    {
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, event);
        throw std::runtime_error("EventD1 decoding failed");
    }

    auto *decodedD1 = decoded->eventId.choice.eventD1_r17;
    if (decoded->eventId.present != ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17 || decodedD1 == nullptr)
    {
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, event);
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, decoded);
        throw std::runtime_error("EventD1 decoded wrong event type");
    }

    EventReferenceLocation decodedLoc1{};
    EventReferenceLocation decodedLoc2{};
    if (!nr::rrc::common::referenceLocationFromAsnValue(decodedD1->referenceLocation1_r17, decodedLoc1) ||
        !nr::rrc::common::referenceLocationFromAsnValue(decodedD1->referenceLocation2_r17, decodedLoc2))
    {
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, event);
        asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, decoded);
        throw std::runtime_error("EventD1 decoded reference location payload is invalid");
    }

    assertEq(decodedD1->distanceThreshFromReference1_r17, 1111, "EventD1 threshold1 mismatch");
    assertEq(decodedD1->distanceThreshFromReference2_r17, 2222, "EventD1 threshold2 mismatch");
    assertEq(decodedD1->hysteresisLocation_r17, 12, "EventD1 hysteresis mismatch");
    assertEq(decodedD1->timeToTrigger, ASN_RRC_TimeToTrigger_ms160, "EventD1 TTT mismatch");

    assertNear(decodedLoc1.latitudeDeg, loc1.latitudeDeg, 5e-5, "EventD1 ref1 latitude mismatch");
    assertNear(decodedLoc1.longitudeDeg, loc1.longitudeDeg, 5e-5, "EventD1 ref1 longitude mismatch");
    assertNear(decodedLoc2.latitudeDeg, loc2.latitudeDeg, 5e-5, "EventD1 ref2 latitude mismatch");
    assertNear(decodedLoc2.longitudeDeg, loc2.longitudeDeg, 5e-5, "EventD1 ref2 longitude mismatch");

    asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, event);
    asn::Free(asn_DEF_ASN_RRC_EventTriggerConfig, decoded);
}

void testCondEventD1ReferenceLocationRoundTrip()
{
    using nr::rrc::common::EventReferenceLocation;

    auto *cond = asn::New<ASN_RRC_CondTriggerConfig_r16>();
    cond->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17;
    asn::MakeNew(cond->condEventId.choice.condEventD1_r17);

    auto *d1 = cond->condEventId.choice.condEventD1_r17;
    d1->distanceThreshFromReference1_r17 = 10;
    d1->distanceThreshFromReference2_r17 = 20;
    d1->hysteresisLocation_r17 = 30;
    d1->timeToTrigger_r17 = ASN_RRC_TimeToTrigger_ms320;

    EventReferenceLocation loc1{};
    loc1.latitudeDeg = 10.1234;
    loc1.longitudeDeg = -130.4321;
    nr::rrc::common::referenceLocationToAsnValue(loc1, d1->referenceLocation1_r17);

    EventReferenceLocation loc2{};
    loc2.latitudeDeg = -42.9876;
    loc2.longitudeDeg = 170.5432;
    nr::rrc::common::referenceLocationToAsnValue(loc2, d1->referenceLocation2_r17);

    cond->rsType_r16 = ASN_RRC_NR_RS_Type_ssb;

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
    if (encoded.length() == 0)
    {
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
        throw std::runtime_error("CondEventD1 encoding failed");
    }

    auto *decoded =
        rrc::encode::Decode<ASN_RRC_CondTriggerConfig_r16>(asn_DEF_ASN_RRC_CondTriggerConfig_r16, encoded);
    if (decoded == nullptr)
    {
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
        throw std::runtime_error("CondEventD1 decoding failed");
    }

    auto *decodedD1 = decoded->condEventId.choice.condEventD1_r17;
    if (decoded->condEventId.present != ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17 ||
        decodedD1 == nullptr)
    {
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, decoded);
        throw std::runtime_error("CondEventD1 decoded wrong event type");
    }

    EventReferenceLocation decodedLoc1{};
    EventReferenceLocation decodedLoc2{};
    if (!nr::rrc::common::referenceLocationFromAsnValue(decodedD1->referenceLocation1_r17, decodedLoc1) ||
        !nr::rrc::common::referenceLocationFromAsnValue(decodedD1->referenceLocation2_r17, decodedLoc2))
    {
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
        asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, decoded);
        throw std::runtime_error("CondEventD1 decoded reference location payload is invalid");
    }

    assertEq(decodedD1->distanceThreshFromReference1_r17, 10, "CondEventD1 threshold1 mismatch");
    assertEq(decodedD1->distanceThreshFromReference2_r17, 20, "CondEventD1 threshold2 mismatch");
    assertEq(decodedD1->hysteresisLocation_r17, 30, "CondEventD1 hysteresis mismatch");
    assertEq(decodedD1->timeToTrigger_r17, ASN_RRC_TimeToTrigger_ms320, "CondEventD1 TTT mismatch");

    assertNear(decodedLoc1.latitudeDeg, loc1.latitudeDeg, 5e-5, "CondEventD1 ref1 latitude mismatch");
    assertNear(decodedLoc1.longitudeDeg, loc1.longitudeDeg, 5e-5, "CondEventD1 ref1 longitude mismatch");
    assertNear(decodedLoc2.latitudeDeg, loc2.latitudeDeg, 5e-5, "CondEventD1 ref2 latitude mismatch");
    assertNear(decodedLoc2.longitudeDeg, loc2.longitudeDeg, 5e-5, "CondEventD1 ref2 longitude mismatch");

    asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, cond);
    asn::Free(asn_DEF_ASN_RRC_CondTriggerConfig_r16, decoded);
}

} // namespace

int main()
{
    try
    {
        testEventD1ReferenceLocationRoundTrip();
        testCondEventD1ReferenceLocationRoundTrip();
    }
    catch (const std::exception &error)
    {
        std::cerr << "rrc_reference_location_tests: FAILED: " << error.what() << std::endl;
        return 1;
    }

    std::cout << "rrc_reference_location_tests: OK" << std::endl;
    return 0;
}