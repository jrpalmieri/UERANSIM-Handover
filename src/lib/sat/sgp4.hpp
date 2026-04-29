
#pragma once

#include "sat_base.hpp"
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>


namespace nr::sat
{

class Propagator
{
    private:
        libsgp4::Tle m_tle;
        libsgp4::SGP4 m_sgp4;

    public:

        explicit Propagator(const SatTleEntry &tleEntry);


        EcefPosition FindPositionEcef(const int64_t unixMillis) const;

        KeplerianElementsRaw GetKeplerianElements(const int64_t unixMillis) const;

    private:
        EcefPosition EciToEcef(const libsgp4::Eci& eci) const;


};
    
int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime);

libsgp4::DateTime UnixMillisToDateTime(int64_t unixMillis);

libsgp4::DateTime ParseTleEpochDateTime(const std::string &tleEpoch, const std::string &fieldPath);


} // namespace nr::sat
