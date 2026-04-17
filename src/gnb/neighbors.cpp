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


void GnbNeighbors::upsertLocked(const GnbNeighborConfig &entry)
{
    auto it = std::find_if(m_neighbors.begin(), m_neighbors.end(), [&entry](const GnbNeighborConfig &e) {
        return e.getPci() == entry.getPci();
    });
    if (it != m_neighbors.end())
        *it = entry;
    else
        m_neighbors.push_back(entry);
}

void GnbNeighbors::upsert(const GnbNeighborConfig &entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    upsertLocked(entry);
}

void GnbNeighbors::upsertAll(const std::vector<GnbNeighborConfig> &entries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : entries)
        upsertLocked(entry);
}

void GnbNeighbors::replaceAll(const std::vector<GnbNeighborConfig> &entries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_neighbors = entries;
}

void GnbNeighbors::remove(int pci)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_neighbors.erase(
        std::remove_if(m_neighbors.begin(), m_neighbors.end(),
                       [pci](const GnbNeighborConfig &e) { return e.getPci() == pci; }),
        m_neighbors.end());
}

std::optional<GnbNeighborConfig> GnbNeighbors::findByPci(int pci) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_neighbors.begin(), m_neighbors.end(), [pci](const GnbNeighborConfig &e) {
        return e.getPci() == pci;
    });
    if (it == m_neighbors.end())
        return std::nullopt;
    return *it;
}

std::optional<GnbNeighborConfig> GnbNeighbors::findByNci(int64_t nci) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_neighbors.begin(), m_neighbors.end(), [nci](const GnbNeighborConfig &e) {
        return e.nci == nci;
    });
    if (it == m_neighbors.end())
        return std::nullopt;
    return *it;
}

std::vector<GnbNeighborConfig> GnbNeighbors::getAll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_neighbors;
}

size_t GnbNeighbors::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_neighbors.size();
}

} // namespace nr::gnb
