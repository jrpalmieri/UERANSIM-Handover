//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utils/common_types.hpp>
#include <utils/json.hpp>
#include <lib/sat/sat_calc.hpp>

namespace nr::rrc::common
{

enum class HandoverEventType
{
    UNKNOWN,
    A2,
    A3,
    A5,
    D1,
    CondA3,
    CondD1,
    CondT1,
};

inline std::string HandoverEventTypeToString(HandoverEventType value)
{
    switch (value)
    {
    case HandoverEventType::UNKNOWN:
        return "UNKNOWN";
    case HandoverEventType::A2:
        return "A2";
    case HandoverEventType::A3:
        return "A3";
    case HandoverEventType::A5:
        return "A5";
    case HandoverEventType::D1:
        return "D1";
    case HandoverEventType::CondA3:
        return "condA3";
    case HandoverEventType::CondD1:
        return "condD1";
    case HandoverEventType::CondT1:
        return "condT1";
    }

    return "A3";
}

inline bool IsMeasurementEvent(HandoverEventType value)
{
    return value == HandoverEventType::A2 || value == HandoverEventType::A3 ||
           value == HandoverEventType::A5 || value == HandoverEventType::D1;
}

inline bool IsConditionalEvent(HandoverEventType value)
{
    return value == HandoverEventType::CondA3 || value == HandoverEventType::CondD1 ||
           value == HandoverEventType::CondT1;
}

inline std::optional<HandoverEventType> ParseHandoverEventType(std::string_view value)
{
    std::string normalized{value};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "a2")
        return HandoverEventType::A2;
    if (normalized == "a3")
        return HandoverEventType::A3;
    if (normalized == "a5")
        return HandoverEventType::A5;
    if (normalized == "d1")
        return HandoverEventType::D1;
    if (normalized == "conda3" || normalized == "condeventa3")
        return HandoverEventType::CondA3;
    if (normalized == "condd1" || normalized == "condeventd1")
        return HandoverEventType::CondD1;
    if (normalized == "condt1" || normalized == "condeventt1")
        return HandoverEventType::CondT1;
    if (normalized == "unknown")
        return HandoverEventType::UNKNOWN;

    return {};
}

// valid TimeToTrigger values in milliseconds
enum class E_TTT_ms {

    ms0 = 0,
    ms40 = 1,
    ms64 = 2,
    ms80 = 3,
    ms100 = 4,
    ms128 = 5,
    ms160 = 6,
    ms256 = 7,
    ms320 = 8,
    ms480 = 9,
    ms512 = 10,
    ms640 = 11,
    ms1024 = 12,
    ms1280 = 13,
    ms2560 = 14,
    ms5120 = 15
};

inline const char* E_TTT_ms_to_string(E_TTT_ms ttt) {
    static const char* const names[] = {"ms0", "ms40", "ms64", "ms80", "ms100", "ms128", "ms160", "ms256", "ms320", "ms480", "ms512", "ms640", "ms1024", "ms1280", "ms2560", "ms5120"};
    return names[static_cast<std::size_t>(ttt)];
}

inline int E_TTT_ms_to_ms(E_TTT_ms ttt) {

    switch (ttt) {
        case E_TTT_ms::ms0: return 0;
        case E_TTT_ms::ms40: return 40;
        case E_TTT_ms::ms64: return 64;
        case E_TTT_ms::ms80: return 80;
        case E_TTT_ms::ms100: return 100;
        case E_TTT_ms::ms128: return 128;
        case E_TTT_ms::ms160: return 160;
        case E_TTT_ms::ms256: return 256;
        case E_TTT_ms::ms320: return 320;
        case E_TTT_ms::ms480: return 480;
        case E_TTT_ms::ms512: return 512;
        case E_TTT_ms::ms640: return 640;
        case E_TTT_ms::ms1024: return 1024;
        case E_TTT_ms::ms1280: return 1280;
        case E_TTT_ms::ms2560: return 2560;
        case E_TTT_ms::ms5120: return 5120;
    }
    return 0;
}


inline E_TTT_ms E_TTT_ms_from_string(const std::string& value) {
    std::string upperValue = value;
    std::transform(upperValue.begin(), upperValue.end(), upperValue.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (upperValue == "MS0") {
        return E_TTT_ms::ms0;
    } else if (upperValue == "MS40") {
        return E_TTT_ms::ms40;
    } else if (upperValue == "MS64") {
        return E_TTT_ms::ms64;
    } else if (upperValue == "MS80") {
        return E_TTT_ms::ms80;
    } else if (upperValue == "MS100") {
        return E_TTT_ms::ms100;
    } else if (upperValue == "MS128") {
        return E_TTT_ms::ms128;
    } else if (upperValue == "MS160") {
        return E_TTT_ms::ms160;
    } else if (upperValue == "MS256") {
        return E_TTT_ms::ms256;
    } else if (upperValue == "MS320") {
        return E_TTT_ms::ms320;
    } else if (upperValue == "MS480") {
        return E_TTT_ms::ms480;
    } else if (upperValue == "MS512") {
        return E_TTT_ms::ms512;
    } else if (upperValue == "MS640") {
        return E_TTT_ms::ms640;
    } else if (upperValue == "MS1024") {
        return E_TTT_ms::ms1024;
    } else if (upperValue == "MS1280") {
        return E_TTT_ms::ms1280;
    } else if (upperValue == "MS2560") {
        return E_TTT_ms::ms2560;
    } else if (upperValue == "MS5120") {
        return E_TTT_ms::ms5120;
    }
    return E_TTT_ms::ms0;
}

struct EventReferenceLocation {
    double latitudeDeg{0.0};
    double longitudeDeg{0.0};
};

class DynamicEventTriggerParams
{
  public:
    int d1_distanceThresholdFromReference1{1000};
    int d1_distanceThresholdFromReference2{1000};
    EventReferenceLocation d1_referenceLocation1{};
    EventReferenceLocation d1_referenceLocation2{};
    int d1_hysteresisLocation{1};

    int condT1_thresholdSec{0};
    int condT1_durationSec{100};

    int condD1_distanceThresholdFromReference1{1000};
    int condD1_distanceThresholdFromReference2{1000};
    EventReferenceLocation condD1_referenceLocation1{};
    EventReferenceLocation condD1_referenceLocation2{};
    int condD1_hysteresisLocation{1};
};

class ReportConfigEvent
{
    public:

        int eventId{0};       // config-file event ID (set at load time, used as map key)
        int reportConfigId{0};

        nr::rrc::common::HandoverEventType eventKind{nr::rrc::common::HandoverEventType::UNKNOWN};

        // Event type string (e.g. "A3", "A2", "D1", "condT1", etc.)
        // Kept for compatibility while migrating callers to eventKind.
        std::string eventType{""};

        // common fields

        E_TTT_ms ttt{E_TTT_ms::ms0};
        int maxReportCells{8};
        
        bool reportOnLeave{true};
        bool useAllowedCellList{false};
        
        // eventA2 fields

        int a2_thresholdDbm{-110};
        int a2_hysteresisDb{1};

        // event a3 fields

        int a3_offsetDb{3};
        int a3_hysteresisDb{1};
        
        // eventA5 fields
        
        int a5_threshold1Dbm{-110};
        int a5_threshold2Dbm{-95};
        int a5_hysteresisDb{1};

        // eventD1-r17 fields

        int d1_distanceThreshFromReference1{1000};
        int d1_distanceThreshFromReference2{1000};
        EventReferenceLocation d1_referenceLocation1{};
        EventReferenceLocation d1_referenceLocation2{};
        int d1_hysteresisLocation{1};

        // condEventT1-r17 fields

        long condT1_thresholdSecTS{0};
        int condT1_durationSec{100};

        // condEventD1-r17 fields

        int condD1_distanceThreshFromReference1{1000};
        int condD1_distanceThreshFromReference2{1000};
        EventReferenceLocation condD1_referenceLocation1{};
        EventReferenceLocation condD1_referenceLocation2{};
        int condD1_hysteresisLocation{1};

        // condEventA3 fields

        int condA3_offsetDb{3};
        int condA3_hysteresisDb{1};


        const char *eventStr() const
        {
            switch (eventKind)
            {
            case nr::rrc::common::HandoverEventType::A2:
                return "A2";
            case nr::rrc::common::HandoverEventType::A3:
                return "A3";
            case nr::rrc::common::HandoverEventType::A5:
                return "A5";
            case nr::rrc::common::HandoverEventType::D1:
                return "D1";
            case nr::rrc::common::HandoverEventType::CondA3:
                return "condA3";
            case nr::rrc::common::HandoverEventType::CondD1:
                return "condD1";
            case nr::rrc::common::HandoverEventType::CondT1:
                return "condT1";
            default:
                return "Unknown";
            }
        }
        
        bool evaluateA2(int servingRsrp) const
        {
            // A2 entering: serving RSRP < threshold - hysteresis
            return servingRsrp < (a2_thresholdDbm - a2_hysteresisDb);
        }

        bool evaluateA3Cell(int servingRsrp, int neighborRsrp) const
        {
            // A3 entering: neighbor RSRP > serving RSRP + offset + hysteresis
            return neighborRsrp > (servingRsrp + a3_offsetDb + a3_hysteresisDb);
        }

        bool evaluateA5Serving(int servingRsrp) const
        {
            // A5 entering condition 1: serving < threshold1 - hysteresis
            return servingRsrp < (a5_threshold1Dbm - a5_hysteresisDb);
        }

        bool evaluateA5Neighbor(int neighborRsrp) const
        {
            // A5 entering condition 2: neighbor > threshold2 + hysteresis
            return neighborRsrp > (a5_threshold2Dbm + a5_hysteresisDb);
        }
        bool evaluateCondA3(int servingRsrp, int neighborRsrp) const
        {
            // condA3: neighbor RSRP > serving RSRP + offset (no hysteresis for conditional events)
            return neighborRsrp > (servingRsrp + a3_offsetDb);
        }
        bool evaluateD1(const GeoPosition &ueLocation) const
        {
            // D1: distance to reference location > threshold
            
            GeoPosition refLoc1{d1_referenceLocation1.latitudeDeg, d1_referenceLocation1.longitudeDeg, 0.0};
            
            double distanceToRef1 = nr::sat::HaversineDistanceMeters(ueLocation, refLoc1);
            if (distanceToRef1 > d1_distanceThreshFromReference1 + d1_hysteresisLocation) {

                GeoPosition refLoc2{d1_referenceLocation2.latitudeDeg, d1_referenceLocation2.longitudeDeg, 0.0};
                double distanceToRef2 = nr::sat::HaversineDistanceMeters(ueLocation, refLoc2);

                return distanceToRef2 < d1_distanceThreshFromReference2 - d1_hysteresisLocation;

            }
            return false;
        }
        bool evaluateCondD1(const GeoPosition &ueLocation) const
        {
            // D1: distance to reference location > threshold
            
            GeoPosition refLoc1{condD1_referenceLocation1.latitudeDeg, condD1_referenceLocation1.longitudeDeg, 0.0};
            
            double distanceToRef1 = nr::sat::HaversineDistanceMeters(ueLocation, refLoc1);
            if (distanceToRef1 > condD1_distanceThreshFromReference1 + condD1_hysteresisLocation) {

                GeoPosition refLoc2{condD1_referenceLocation2.latitudeDeg, condD1_referenceLocation2.longitudeDeg, 0.0};
                double distanceToRef2 = nr::sat::HaversineDistanceMeters(ueLocation, refLoc2);

                return distanceToRef2 < condD1_distanceThreshFromReference2 - condD1_hysteresisLocation;

            }
            return false;
        }
        bool evaluateCondT1(long currentTS) const
        {
            // T1: current time > thresholdSecTS AND current time < thresholdSecTS + durationSec
            return (currentTS > condT1_thresholdSecTS) && (currentTS < (condT1_thresholdSecTS + condT1_durationSec));
        }

        int timeToTriggerMs() const
        {
            return E_TTT_ms_to_ms(ttt);
        }

        Json toJson() const
        {
            const auto locationToJson = [](const EventReferenceLocation &loc) {
                return Json::Obj({
                    {"latitudeDeg", std::to_string(loc.latitudeDeg)},
                    {"longitudeDeg", std::to_string(loc.longitudeDeg)},
                });
            };

            return Json::Obj({
                {"eventId", eventId},
                {"eventKind", HandoverEventTypeToString(eventKind)},
                {"eventType", eventType},
                {"ttt", std::string(E_TTT_ms_to_string(ttt))},
                {"timeToTriggerMs", timeToTriggerMs()},
                {"reportOnLeave", reportOnLeave},
                {"useAllowedCellList", useAllowedCellList},
                {"maxReportCells", maxReportCells},

                {"a2_thresholdDbm", a2_thresholdDbm},
                {"a2_hysteresisDb", a2_hysteresisDb},

                {"a3_offsetDb", a3_offsetDb},
                {"a3_hysteresisDb", a3_hysteresisDb},

                {"a5_threshold1Dbm", a5_threshold1Dbm},
                {"a5_threshold2Dbm", a5_threshold2Dbm},
                {"a5_hysteresisDb", a5_hysteresisDb},

                {"d1_distanceThreshFromReference1", d1_distanceThreshFromReference1},
                {"d1_distanceThreshFromReference2", d1_distanceThreshFromReference2},
                {"d1_referenceLocation1", locationToJson(d1_referenceLocation1)},
                {"d1_referenceLocation2", locationToJson(d1_referenceLocation2)},
                {"d1_hysteresisLocation", d1_hysteresisLocation},

                {"condT1_thresholdSecTS", condT1_thresholdSecTS},
                {"condT1_durationSec", condT1_durationSec},

                {"condD1_distanceThreshFromReference1", condD1_distanceThreshFromReference1},
                {"condD1_distanceThreshFromReference2", condD1_distanceThreshFromReference2},
                {"condD1_referenceLocation1", locationToJson(condD1_referenceLocation1)},
                {"condD1_referenceLocation2", locationToJson(condD1_referenceLocation2)},
                {"condD1_hysteresisLocation", condD1_hysteresisLocation},

                {"condA3_offsetDb", condA3_offsetDb},
                {"condA3_hysteresisDb", condA3_hysteresisDb},
            });
        }

};

struct MeasObject
{
    int measObjectId{};
    int ssbFrequency{};     // SSB ARFCN (simplified — we use cellId matching instead)

};


/**
 * @brief Struct to represent a Measurement Identity, which binds together:
 * - measId: the identifier used in MeasurementReport messages
 * - measObjectId: the measurement object (frequency) to which this measId applies
 * - reportConfigId: the report configuration (event trigger) that uses this measId
 *
 */
struct MeasIdentity
{
    int measId{};           // maps to the measId in MeasurementReport messages
    int measObjectId{};     // maps to the measObjectId in UeMeasObject, 
                            //  which defines the frequency to measure
    int reportConfigId{};   // maps to the reportConfigId in UeReportConfig, 
                            //  which defines the event trigger conditions for this measId
};

} // namespace nr::rrc::common
