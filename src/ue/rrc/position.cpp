// //
// // UE Position – JSON serialization and utility implementations.
// //

// #include "position.hpp"

// #include <cstdio>

// namespace nr::ue
// {

// static Json doubleToJson(double v)
// {
//     char buf[64];
//     std::snprintf(buf, sizeof(buf), "%.6f", v);
//     return std::string(buf);
// }

// Json ToJson(const GeoPosition &v)
// {
//     return Json::Obj({
//         {"latitude", doubleToJson(v.latitude)},
//         {"longitude", doubleToJson(v.longitude)},
//         {"altitude", doubleToJson(v.altitude)},
//     });
// }

// Json ToJson(const EcefPosition &v)
// {
//     return Json::Obj({
//         {"x", doubleToJson(v.x)},
//         {"y", doubleToJson(v.y)},
//         {"z", doubleToJson(v.z)},
//     });
// }

// Json ToJson(const UePosition &v)
// {
//     return Json::Obj({
//         {"geo", ToJson(v.geo)},
//         {"ecef", ToJson(v.ecef)},
//     });
// }

// } // namespace nr::ue
