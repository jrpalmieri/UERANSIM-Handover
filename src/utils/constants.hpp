//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <cstdint>

struct cons
{
    // Version information
    static constexpr const uint8_t Major = 3;
    static constexpr const uint8_t Minor = 3;
    static constexpr const uint8_t Patch = 7;
    static constexpr const char *Project = "UERANSIM";
    static constexpr const char *Tag = "v3.3.7";
    static constexpr const char *Name = "UERANSIM v3.3.7";
    static constexpr const char *Owner = "ALİ GÜNGÖR";

    // Some port values
    static constexpr const uint16_t GtpPort = 2152;
    static constexpr const uint16_t RadioLinkPort = 4997;

    // TUN interface
    static constexpr const char *TunNamePrefix = "uesimtun";
    static constexpr const char *TunNetmask = "255.255.0.0";
    static constexpr const int TunMtu = 1400;

    // Constraints
    static constexpr const int MinNodeName = 3;
    static constexpr const int MaxNodeName = 1024;

    // Others
    static constexpr const char *CMD_SERVER_IP = "127.0.0.1";
    static constexpr const char *PROC_TABLE_DIR = "/tmp/UERANSIM.proc-table/";
    static constexpr const char *PROCESS_DIR = "/proc/";
    static constexpr const char DIR_SEPARATOR = '/';

    // RF related
    // mask used to generate PCI from NCI (PCI is the last 10 bits of NCI)
    static constexpr const int PCI_MASK = 0x3FF;

    // Extract PCI from a 36-bit NCI (lower 10 bits)
    static constexpr int getPciFromNci(int64_t nci)
    {
        return static_cast<int>(nci & PCI_MASK);
    }
    // Min value for RSRP (equivalent to no signal)
    static constexpr const int MIN_RSRP = -156;
    // Max value for RSRP (equivalent to strongest possible signal)
    static constexpr const int MAX_RSRP = -44;
    // Threshold signal level to trigger radio link failure 
    //   (this is a tunable parameter that can be adjusted based on testing;
    //   -120 is a commonly used value in practice)
    static constexpr const int RLF_RSRP = -120;
};
