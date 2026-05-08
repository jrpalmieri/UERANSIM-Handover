# Satellite Functions


## Relevant Files
sat_time.cpp
sat_state.cpp
sat_calc.cpp
sgp4.cpp
sat_base.cpp




## Available Classes / Structs

SatTime - Satellite Time Clock
SatStates - Store of satellite TLEs and propagators

EcefPosition
SatTleEntry - a Sat TLE
KeplerianElementsRaw - teh set of six Keplerian elements

EphEntry - struct that represents the content of a SIB19 entry

## Helper Functions

GeoToEcef() - converts a WGS84 coordinate set (GeoPosition) to an ECEF coordinate set (EcefPosition)
EcefToGeo() - converts an ECEF coordinate set (EcefPosition) to a WGS84 coordinate set (GeoPosition)
EcefDistance() - calculates a Euclidian distance between two Ecef coordinates (EcefPosition)
EcefDistanceSquared() - calculates a Euclidian distance squared between two Ecef coordinates (EcefPosition)
ElevationAngleDeg() - calculates the elevation angle between a ground observer and a satellite using ECEF coordinates (EcefPosition)
ComputeNadir() - calculates the nadir (sub-satellite-point) of a satellite using ECEF coordinates (EcefPosition)



## Include Map

sat_state.hpp    sat_calc.hpp               sat_time.hpp
    |                |
    |                |
    |________________|
    |                
    |
  sgp4.hpp
    |
    |
sat_base.hpp


## Usage

Satellite functions 