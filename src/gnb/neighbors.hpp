//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <string>
#include <vector>

#include <gnb/types.hpp>

namespace YAML
{
class Node;
}

namespace nr::gnb
{

EHandoverInterface ReadNeighborHandoverInterface(const YAML::Node &node, const std::string &fieldPath);

GnbNeighborConfig ReadNeighborConfig(const YAML::Node &neighborNode,
                                     bool fullRecordRequired,
                                     const std::string &entryPath);

void ValidateUniqueNeighborPci(const std::vector<GnbNeighborConfig> &neighbors, const std::string &listPath);

} // namespace nr::gnb
