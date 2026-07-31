//
// Round-trip tests for the simulator's custom source-to-target handover container
// (gnb/rrc/handover_container.hpp + the payload codec in gnb/rrc/handover.cpp).
//
// The container is what carries the UE's RRC context from the source gNB to the
// target, so a silent asymmetry between the encoder and the decoder shows up only as
// a target-side handover rejection.  These tests pin the round trip down directly.
//

#include <gnb/rrc/handover_container.hpp>
#include <gnb/types.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{

using nr::gnb::RrcUeContext;
using nr::rrc::common::HandoverEventType;

void assertTrue(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void assertEq(long long actual, long long expected, const std::string &message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message + " expected=" + std::to_string(expected) +
                                 " actual=" + std::to_string(actual));
    }
}

void assertEqExact(double actual, double expected, const std::string &message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message + " expected=" + std::to_string(expected) +
                                 " actual=" + std::to_string(actual));
    }
}

// A source-side context with every serialized field set to a distinct, non-default
// value, so a field written or read out of order cannot go unnoticed.
RrcUeContext makeSourceContext()
{
    RrcUeContext ue{static_cast<int64_t>(0x0123456789ABCDEFLL)};
    ue.cRnti = 0x4321;
    ue.nextHopChainingCount = 5;
    for (size_t i = 0; i < ue.nextHopParameter.size(); ++i)
        ue.nextHopParameter[i] = static_cast<uint8_t>(0xA0 + i);

    ue.ueSecInfo.nRencryptionAlgorithmsBitmap = 0x8000;
    ue.ueSecInfo.eUTRAencryptionAlgorithmsBitmap = 0x4000;
    ue.ueSecInfo.nRintegrityProtectionAlgorithmsBitmap = 0x2000;
    ue.ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap = 0x1000;
    for (size_t i = 0; i < ue.ueSecInfo.k_gnb.size(); ++i)
        ue.ueSecInfo.k_gnb[i] = static_cast<uint8_t>(0x10 + i);

    RrcUeContext::MeasIdentityMappings measId1{};
    measId1.measId = 1;
    measId1.measObjectId = 11;
    measId1.reportConfigId = 21;
    measId1.eventKind = HandoverEventType::A3;
    measId1.eventType = "A3";
    measId1.choProfileId = -1;
    ue.measIdentities[measId1.measId] = measId1;

    RrcUeContext::MeasIdentityMappings measId2{};
    measId2.measId = 2;
    measId2.measObjectId = 12;
    measId2.reportConfigId = 22;
    measId2.eventKind = HandoverEventType::CondD1;
    measId2.eventType = "condD1";
    measId2.choProfileId = 3;
    ue.measIdentities[measId2.measId] = measId2;

    nr::rrc::common::ReportConfigEvent rc{};
    rc.eventId = 7;
    rc.reportConfigId = 21;
    rc.eventKind = HandoverEventType::A3;
    rc.eventType = "A3";
    rc.ttt = nr::rrc::common::E_TTT_ms::ms640;
    rc.maxReportCells = 4;
    rc.reportOnLeave = false;
    rc.useAllowedCellList = true;
    rc.a2_thresholdDbm = -101;
    rc.a2_hysteresisDb = 2;
    rc.a3_offsetDb = -3;
    rc.a3_hysteresisDb = 4;
    rc.a5_threshold1Dbm = -105;
    rc.a5_threshold2Dbm = -96;
    rc.a5_hysteresisDb = 6;
    rc.d1_distanceThreshFromReference1 = 1500;
    rc.d1_distanceThreshFromReference2 = 2500;
    rc.d1_referenceLocation1 = {41.5, -71.25};
    rc.d1_referenceLocation2 = {-33.75, 151.125};
    rc.d1_hysteresisLocation = 7;
    rc.condT1_thresholdSecTS = 1234567890123LL;
    rc.condT1_durationSec = 88;
    rc.condD1_distanceThreshFromReference1 = 3500;
    rc.condD1_distanceThreshFromReference2 = 4500;
    rc.condD1_referenceLocation1 = {12.0625, 34.125};
    rc.condD1_referenceLocation2 = {-56.5, -78.75};
    rc.condD1_hysteresisLocation = 9;
    rc.condA3_offsetDb = 11;
    rc.condA3_hysteresisDb = 12;
    ue.reportConfigEvents[rc.reportConfigId] = rc;

    nr::rrc::common::MeasObject mo{};
    mo.measObjectId = 11;
    mo.ssbFrequency = 632448;
    ue.measObjects[mo.measObjectId] = mo;

    return ue;
}

void assertContextsMatch(const RrcUeContext &decoded, const RrcUeContext &source)
{
    assertEq(decoded.ueId, source.ueId, "ueId");
    assertEq(decoded.cRnti, source.cRnti, "cRnti");
    assertEq(decoded.nextHopChainingCount, source.nextHopChainingCount, "nextHopChainingCount");
    assertTrue(decoded.nextHopParameter == source.nextHopParameter, "nextHopParameter mismatch");

    assertEq(decoded.ueSecInfo.nRencryptionAlgorithmsBitmap, source.ueSecInfo.nRencryptionAlgorithmsBitmap,
             "nRencryptionAlgorithmsBitmap");
    assertEq(decoded.ueSecInfo.eUTRAencryptionAlgorithmsBitmap, source.ueSecInfo.eUTRAencryptionAlgorithmsBitmap,
             "eUTRAencryptionAlgorithmsBitmap");
    assertEq(decoded.ueSecInfo.nRintegrityProtectionAlgorithmsBitmap,
             source.ueSecInfo.nRintegrityProtectionAlgorithmsBitmap, "nRintegrityProtectionAlgorithmsBitmap");
    assertEq(decoded.ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap,
             source.ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap, "eUTRAintegrityProtectionAlgorithmsBitmap");
    assertTrue(decoded.ueSecInfo.k_gnb == source.ueSecInfo.k_gnb, "k_gnb mismatch");

    assertEq(decoded.measIdentities.size(), source.measIdentities.size(), "measIdentities count");
    for (const auto &[key, expected] : source.measIdentities)
    {
        auto it = decoded.measIdentities.find(key);
        assertTrue(it != decoded.measIdentities.end(), "measIdentity " + std::to_string(key) + " missing");
        const auto &actual = it->second;
        assertEq(actual.measId, expected.measId, "measId");
        assertEq(actual.measObjectId, expected.measObjectId, "measObjectId");
        assertEq(actual.reportConfigId, expected.reportConfigId, "reportConfigId");
        assertEq(static_cast<int>(actual.eventKind), static_cast<int>(expected.eventKind), "measIdentity eventKind");
        assertTrue(actual.eventType == expected.eventType, "measIdentity eventType mismatch");
        assertEq(actual.choProfileId, expected.choProfileId, "choProfileId");
    }

    assertEq(decoded.reportConfigEvents.size(), source.reportConfigEvents.size(), "reportConfigEvents count");
    for (const auto &[key, expected] : source.reportConfigEvents)
    {
        auto it = decoded.reportConfigEvents.find(key);
        assertTrue(it != decoded.reportConfigEvents.end(), "reportConfigEvent " + std::to_string(key) + " missing");
        const auto &actual = it->second;
        assertEq(actual.eventId, expected.eventId, "eventId");
        assertEq(actual.reportConfigId, expected.reportConfigId, "reportConfig reportConfigId");
        assertEq(static_cast<int>(actual.eventKind), static_cast<int>(expected.eventKind), "reportConfig eventKind");
        assertTrue(actual.eventType == expected.eventType, "reportConfig eventType mismatch");
        assertEq(static_cast<int>(actual.ttt), static_cast<int>(expected.ttt), "ttt");
        assertEq(actual.maxReportCells, expected.maxReportCells, "maxReportCells");
        assertEq(actual.reportOnLeave, expected.reportOnLeave, "reportOnLeave");
        assertEq(actual.useAllowedCellList, expected.useAllowedCellList, "useAllowedCellList");
        assertEq(actual.a2_thresholdDbm, expected.a2_thresholdDbm, "a2_thresholdDbm");
        assertEq(actual.a2_hysteresisDb, expected.a2_hysteresisDb, "a2_hysteresisDb");
        assertEq(actual.a3_offsetDb, expected.a3_offsetDb, "a3_offsetDb");
        assertEq(actual.a3_hysteresisDb, expected.a3_hysteresisDb, "a3_hysteresisDb");
        assertEq(actual.a5_threshold1Dbm, expected.a5_threshold1Dbm, "a5_threshold1Dbm");
        assertEq(actual.a5_threshold2Dbm, expected.a5_threshold2Dbm, "a5_threshold2Dbm");
        assertEq(actual.a5_hysteresisDb, expected.a5_hysteresisDb, "a5_hysteresisDb");
        assertEq(actual.d1_distanceThreshFromReference1, expected.d1_distanceThreshFromReference1,
                 "d1_distanceThreshFromReference1");
        assertEq(actual.d1_distanceThreshFromReference2, expected.d1_distanceThreshFromReference2,
                 "d1_distanceThreshFromReference2");
        assertEqExact(actual.d1_referenceLocation1.latitudeDeg, expected.d1_referenceLocation1.latitudeDeg,
                 "d1_referenceLocation1.latitudeDeg");
        assertEqExact(actual.d1_referenceLocation1.longitudeDeg, expected.d1_referenceLocation1.longitudeDeg,
                 "d1_referenceLocation1.longitudeDeg");
        assertEqExact(actual.d1_referenceLocation2.latitudeDeg, expected.d1_referenceLocation2.latitudeDeg,
                 "d1_referenceLocation2.latitudeDeg");
        assertEqExact(actual.d1_referenceLocation2.longitudeDeg, expected.d1_referenceLocation2.longitudeDeg,
                 "d1_referenceLocation2.longitudeDeg");
        assertEq(actual.d1_hysteresisLocation, expected.d1_hysteresisLocation, "d1_hysteresisLocation");
        assertEq(actual.condT1_thresholdSecTS, expected.condT1_thresholdSecTS, "condT1_thresholdSecTS");
        assertEq(actual.condT1_durationSec, expected.condT1_durationSec, "condT1_durationSec");
        assertEq(actual.condD1_distanceThreshFromReference1, expected.condD1_distanceThreshFromReference1,
                 "condD1_distanceThreshFromReference1");
        assertEq(actual.condD1_distanceThreshFromReference2, expected.condD1_distanceThreshFromReference2,
                 "condD1_distanceThreshFromReference2");
        assertEqExact(actual.condD1_referenceLocation1.latitudeDeg, expected.condD1_referenceLocation1.latitudeDeg,
                 "condD1_referenceLocation1.latitudeDeg");
        assertEqExact(actual.condD1_referenceLocation1.longitudeDeg, expected.condD1_referenceLocation1.longitudeDeg,
                 "condD1_referenceLocation1.longitudeDeg");
        assertEqExact(actual.condD1_referenceLocation2.latitudeDeg, expected.condD1_referenceLocation2.latitudeDeg,
                 "condD1_referenceLocation2.latitudeDeg");
        assertEqExact(actual.condD1_referenceLocation2.longitudeDeg, expected.condD1_referenceLocation2.longitudeDeg,
                 "condD1_referenceLocation2.longitudeDeg");
        assertEq(actual.condD1_hysteresisLocation, expected.condD1_hysteresisLocation, "condD1_hysteresisLocation");
        assertEq(actual.condA3_offsetDb, expected.condA3_offsetDb, "condA3_offsetDb");
        assertEq(actual.condA3_hysteresisDb, expected.condA3_hysteresisDb, "condA3_hysteresisDb");
    }

    assertEq(decoded.measObjects.size(), source.measObjects.size(), "measObjects count");
    for (const auto &[key, expected] : source.measObjects)
    {
        auto it = decoded.measObjects.find(key);
        assertTrue(it != decoded.measObjects.end(), "measObject " + std::to_string(key) + " missing");
        assertEq(it->second.measObjectId, expected.measObjectId, "measObjectId");
        assertEq(it->second.ssbFrequency, expected.ssbFrequency, "ssbFrequency");
    }
}

// The payload codec on its own: everything the source writes comes back at the target.
void testRrcContextPayloadRoundTrip()
{
    const RrcUeContext source = makeSourceContext();

    auto payload = nr::gnb::ho_container::EncodeRrcContext(source);
    assertTrue(payload.length() > 0, "EncodeRrcContext produced an empty payload");

    std::unique_ptr<RrcUeContext> decoded{nr::gnb::ho_container::DecodeRrcContext(payload)};
    assertTrue(decoded != nullptr, "DecodeRrcContext rejected its own encoder's output");

    assertContextsMatch(*decoded, source);
}

// The full source-side build / target-side parse, as the two gNBs perform it.
void testFramedContainerRoundTrip()
{
    using namespace nr::gnb::ho_container;

    const RrcUeContext source = makeSourceContext();
    const auto payload = EncodeRrcContext(source);

    for (bool cho : {false, true})
    {
        const uint32_t padding = cho ? 512u : 0u;
        auto container = WrapSourceToTarget(payload, padding, cho);
        assertEq(container.length(), S2T_HEADER_SIZE + payload.length() + static_cast<int>(padding),
                 "framed container length");

        OctetString unwrapped{};
        bool choOut = !cho;
        assertTrue(UnwrapSourceToTarget(container, &unwrapped, &choOut), "UnwrapSourceToTarget rejected a valid frame");
        assertEq(choOut, cho, "CHO indication did not survive the frame");
        assertTrue(unwrapped == payload, "payload did not survive the frame");

        std::unique_ptr<RrcUeContext> decoded{DecodeRrcContext(unwrapped)};
        assertTrue(decoded != nullptr, "DecodeRrcContext rejected a payload taken from a valid frame");
        assertContextsMatch(*decoded, source);
    }
}

// A receiver must reject anything that is not this format rather than parse it as one.
// The regression this guards: the whole framed container (or, on N2, the NGAP container
// wrapping it) being fed straight to the payload decoder, which reads a ueId from
// offset 0 and so read the ASCII magic "S2TC" as the UE identity.
void testMalformedContainersAreRejected()
{
    using namespace nr::gnb::ho_container;

    const RrcUeContext source = makeSourceContext();
    const auto payload = EncodeRrcContext(source);
    const auto container = WrapSourceToTarget(payload, 0, false);

    OctetString out{};

    assertTrue(!UnwrapSourceToTarget(OctetString{}, &out, nullptr), "empty container accepted");
    assertTrue(!UnwrapSourceToTarget(container.subCopy(0, S2T_HEADER_SIZE - 1), &out, nullptr),
               "container shorter than its header accepted");
    assertTrue(!UnwrapSourceToTarget(payload, &out, nullptr), "unframed payload accepted as a container");
    assertTrue(!UnwrapSourceToTarget(container.subCopy(1), &out, nullptr), "container with a shifted magic accepted");
    assertTrue(!UnwrapSourceToTarget(container.subCopy(0, container.length() - 1), &out, nullptr),
               "container truncated inside its payload accepted");

    // A frame whose version this build does not know must be rejected, not misparsed.
    OctetString wrongVersion = container.copy();
    wrongVersion.data()[4] = static_cast<uint8_t>(S2T_VERSION + 1);
    assertTrue(!UnwrapSourceToTarget(wrongVersion, &out, nullptr), "container with an unknown version accepted");

    // Truncated payloads must not produce a half-populated context.
    for (int length : {0, 1, 87, S2T_HEADER_SIZE})
    {
        std::unique_ptr<RrcUeContext> decoded{DecodeRrcContext(payload.subCopy(0, length))};
        assertTrue(decoded == nullptr,
                   "DecodeRrcContext accepted a payload truncated to " + std::to_string(length) + " bytes");
    }
}

} // namespace

int main()
{
    try
    {
        testRrcContextPayloadRoundTrip();
        testFramedContainerRoundTrip();
        testMalformedContainersAreRejected();
    }
    catch (const std::exception &error)
    {
        std::cerr << "handover_container_tests: FAILED: " << error.what() << std::endl;
        return 1;
    }

    std::cout << "handover_container_tests: OK" << std::endl;
    return 0;
}
