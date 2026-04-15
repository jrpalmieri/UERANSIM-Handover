//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "neighbors.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

#include <utils/yaml_utils.hpp>
#include <yaml-cpp/yaml.h>

namespace nr::gnb
{

static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

EHandoverInterface ReadNeighborHandoverInterface(const YAML::Node &node, const std::string &fieldPath)
{
    auto value = ToLower(node.as<std::string>());

    if (value == "n2")
        return EHandoverInterface::N2;
    if (value == "xn")
        return EHandoverInterface::Xn;

    throw std::runtime_error("Field " + fieldPath + " has invalid value, expected N2 or Xn");
}

GnbNeighborConfig ReadNeighborConfig(const YAML::Node &neighborNode,
                                     bool fullRecordRequired,
                                     const std::string &entryPath)
{
    if (!neighborNode.IsMap())
        throw std::runtime_error("Each " + entryPath + " entry must be a map");

    GnbNeighborConfig neighbor{};
    neighbor.nci = yaml::GetInt64(neighborNode, "nci", 0, 0xFFFFFFFFFll);
    neighbor.idLength = yaml::GetInt32(neighborNode, "idLength", 22, 32);

    if (fullRecordRequired)
    {
        neighbor.tac = yaml::GetInt32(neighborNode, "tac", 0, 0xFFFFFF);
        neighbor.ipAddress = yaml::GetIpAddress(neighborNode, "ipAddress");
    }

    if (yaml::HasField(neighborNode, "handoverInterface"))
    {
        neighbor.handoverInterface = ReadNeighborHandoverInterface(neighborNode["handoverInterface"],
                                                                   entryPath + ".handoverInterface");
    }

    if (yaml::HasField(neighborNode, "xnAddress"))
        neighbor.xnAddress = yaml::GetIpAddress(neighborNode, "xnAddress");

    if (yaml::HasField(neighborNode, "xnPort"))
        neighbor.xnPort = static_cast<uint16_t>(yaml::GetInt32(neighborNode, "xnPort", 1, 65535));

    return neighbor;
}

void ValidateUniqueNeighborPci(const std::vector<GnbNeighborConfig> &neighbors, const std::string &listPath)
{
    std::unordered_set<int> seenNeighborPci{};

    for (const auto &neighbor : neighbors)
    {
        auto pci = neighbor.getPci();
        if (seenNeighborPci.count(pci) != 0)
            throw std::runtime_error(listPath + " contains duplicate PCI=" + std::to_string(pci));

        seenNeighborPci.insert(pci);
    }
}

} // namespace nr::gnb
