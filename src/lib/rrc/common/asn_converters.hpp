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
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include <OCTET_STRING.h>

#include <lib/asn/utils.hpp>
#include <utils/bit_buffer.hpp>
#include <utils/octet_string.hpp>
#include <lib/rrc/common/event_types.hpp>

namespace nr::rrc::common
{

static constexpr long NTP_EPOCH_TO_UNIX_EPOCH = 2208988800; // number of seconds from Jan 1, 1900 to Jan 1, 1970



// convert a Measure Trigger Quantity (generally an RSRP) in dBm to the ASN MTQ
//      which is an integer representing 1 dBm starting at -156dBm
//      with valid range of 0..127  (i.e. -156..-29 dBm)
inline long mtqToASNValue(int dbm)
{
    int val = dbm + 156;
    if (val < 0)
        val = 0;
    if (val > 127)
        val = 127;
    return static_cast<long>(val);
}

// convert an ASN MTQ value back to dBm
inline int mtqFromASNValue(long val)
{
    return static_cast<int>(val) - 156;
    
}


// convert from dB to the ASN MeasTriggerQuantityOffset
//      which is an integer representing 0.5 dB steps, 
//      with valid range of 0..30 (i.e. 0..15 dB)
inline long mtqOffsetToASNValue(int offsetDb)
{

    int val = offsetDb * 2;
    if (val < 0)
        val = 0;
    if (val > 30)
        val = 30;
    return static_cast<long>(val);
}

// convert from ASN MeasTriggerQuantityOffset back to dB
inline int mtqOffsetFromASNValue(long val)
{
    return static_cast<int>(val) / 2;
}


// convert a hysteresis value to an ASN Hysteresis 
//      which is an integer representing 0.5 dB steps, 
//      with valid range of 0..30 (i.e. 0..15 dB)
inline long hysteresisToASNValue(int db)
{
    int val = db * 2;
    if (val < 0)
        val = 0;
    if (val > 30)
        val = 30;
    return static_cast<long>(val);
}

// convert an ASN Hysteresis value back to dB
inline int hysteresisFromASNValue(long val)
{
    return static_cast<int>(val) / 2;
}


// Convert an E_TTT_ms enum to the corresponding ASN_RRC_TimeToTrigger enum value
inline long tttMsToASNValue(E_TTT_ms tttMs)
{

    // the enum now matches the ASN values directly, so we can just static_cast it
    return static_cast<long>(tttMs);
}


// convert a hysteresis location in meters to an ASN HysteresisLocation
//      which is an integer representing 10m steps, 
//      with valid range of 0..32768 (i.e. 0..327680 m)
inline long hysteresisLocationToASNValue(int hysteresisLocation)
{
    int val = hysteresisLocation / 10;
    if (val < 0)
        val = 0;
    if (val > 32768)
        val = 32768;
    return static_cast<long>(val);
}

// convert an ASN HysteresisLocation back to meters
inline int hysteresisLocationFromASNValue(long val)
{
    return static_cast<int>(val) * 10;
}

// converts a distanceThreshold in meters to the ASN Value,
//      which is an integer representing 50m steps,
//      with valid range of 1..65525 (i.e. 50..3,276,250 m)
inline long distanceThresholdToASNValue(int distanceM)
{
    int val = distanceM / 50;
    if (val < 1)
        val = 1;
    if (val > 65525)
        val = 65525;
    return static_cast<long>(val);
}

// convert an ASN distanceThreshold value back to meters
inline int distanceThresholdFromASNValue(long val)
{
    return static_cast<int>(val) * 50;
}

// converts a T1 duration in second to the ASN Value, 
//      which is an integer representing 100ms steps, 
//      with valid range of 0..6000 (i.e. 0..600s)
inline long durationToASNValue(int durationSec)
{
    int val = durationSec * 10; // convert from seconds to 100ms steps
    if (val < 1)
        val = 1;
    if (val > 6000)
        val = 6000;
    return static_cast<long>(val);
}

// converts an ASN t1 duration value back to seconds
inline int durationFromASNValue(long val)
{
    return static_cast<int>(val) / 10;
}

// converts a T1 threshold in ms to the ASN Value
//      which is an integer representing 10ms steps,
//      with valid range of 0..549755813887 (i.e. 0..5497558138870 ms)
//      Note: starting point is January 1, 1900, 00:00:00 UTC (NTP Epoch),
//          so this is an absolute timestamp rather than a relative duration.
//
//      This function will convert the unix epoch-based value to the 
//          NTP Epoch value, then apply the unit scaling.
inline long t1ThresholdToASNValue(uint64_t thresholdSec)
{
    uint64_t val = thresholdSec + NTP_EPOCH_TO_UNIX_EPOCH; // convert from unix epoch to NTP epoch
    val = val * 100; // convert from seconds to 10ms steps
    if (val > 549755813887)
        val = 549755813887;
    return static_cast<long>(val);
}

// convert an ASN T1 threshold value back to seconds.
//    also converts from NTP epoch back to unix epoch by subtracting the offset
inline uint64_t t1ThresholdFromASNValue(long val)
{
    return (static_cast<uint64_t>(val) / 100) - NTP_EPOCH_TO_UNIX_EPOCH;
}

inline void referenceLocationToAsnValue(const EventReferenceLocation &location, OCTET_STRING_t &target)
{
    constexpr int kLatBits = 23;
    constexpr int kLonBits = 24;
    constexpr int kLatMax = (1 << kLatBits) - 1;
    constexpr int kLonOffset = 1 << (kLonBits - 1);
    constexpr int kLonMin = -kLonOffset;
    constexpr int kLonMax = kLonOffset - 1;

    const double clampedLatitude = std::clamp(location.latitudeDeg, -90.0, 90.0);
    const double clampedLongitude = std::clamp(location.longitudeDeg, -180.0, 180.0);

    const int latitudeSign = clampedLatitude < 0.0 ? 1 : 0;
    const int degreesLatitude = std::clamp(static_cast<int>(
                                            std::llround(std::fabs(clampedLatitude) * 8388608.0 / 90.0)),
        0,
        kLatMax);
    const int degreesLongitude = std::clamp(static_cast<int>(std::llround(clampedLongitude * 16777216.0 / 360.0)),
        kLonMin,
        kLonMax);

    uint8_t encoded[6] = {0, 0, 0, 0, 0, 0};
    BitBuffer bitBuffer{encoded};
    bitBuffer.writeBits(latitudeSign, 1);
    bitBuffer.writeBits(degreesLatitude, kLatBits);
    bitBuffer.writeBits(degreesLongitude + kLonOffset, kLonBits);

    asn::SetOctetString(target, OctetString::FromArray(encoded, 6));
}

inline bool referenceLocationFromAsnValue(const OCTET_STRING_t &source, EventReferenceLocation &location)
{
    if (source.buf == nullptr || source.size != 6)
        return false;

    constexpr int kLatBits = 23;
    constexpr int kLonBits = 24;
    constexpr int kLonOffset = 1 << (kLonBits - 1);

    BitBuffer bitBuffer{source.buf};
    const int latitudeSign = bitBuffer.readBits(1);
    const int degreesLatitude = bitBuffer.readBits(kLatBits);
    const int degreesLongitude = bitBuffer.readBits(kLonBits) - kLonOffset;

    double latitudeDeg = static_cast<double>(degreesLatitude) * 90.0 / 8388608.0;
    if (latitudeSign == 1)
        latitudeDeg = -latitudeDeg;

    const double longitudeDeg = static_cast<double>(degreesLongitude) * 360.0 / 16777216.0;

    location.latitudeDeg = latitudeDeg;
    location.longitudeDeg = longitudeDeg;
    return true;
}


inline long t304MsToEnum(int ms)
{
    switch (ms)
    {
    case 50:    return 0;
    case 100:   return 1;
    case 150:   return 2;
    case 200:   return 3;
    case 500:   return 4;
    case 1000:  return 5;
    case 2000:  return 6;
    case 10000: return 7;
    default:    return 5; // default ms1000
    }
}


inline int t304EnumToMs(long t304)
{
    static const int table[] = {50, 100, 150, 200, 500, 1000, 2000, 10000};
    if (t304 >= 0 && t304 < 8)
        return table[t304];
    return 1000;
}

} // namespace nr::rrc::common