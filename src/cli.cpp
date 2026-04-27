//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <lib/app/cli_base.hpp>
#include <lib/app/proc_table.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/io.hpp>
#include <utils/network.hpp>
#include <utils/options.hpp>

static struct Options
{
    bool dumpNodes{};
    std::string nodeName{};
    std::string directCmd{};
    bool massDeregister{};
    std::string massDeregCause{};
    std::string massDeregFile{};
} g_options{};

static std::set<int> FindProcesses()
{
    std::set<int> res{};
    for (const auto &file : io::GetEntries(cons::PROCESS_DIR))
    {
        if (!io::IsRegularFile(file))
        {
            auto name = io::GetStem(file);
            if (!utils::IsNumeric(name))
                continue;
            int pid = utils::ParseInt(name);
            res.insert(pid);
        }
    }
    return res;
}

static uint16_t DiscoverNode(const std::string &node, int &skippedDueToVersion)
{
    if (!io::Exists(cons::PROC_TABLE_DIR))
        return 0;

    // Find all processes in the environment
    auto processes = FindProcesses();

    // Read and parse ProcTable entries.
    std::unordered_map<std::string, app::ProcTableEntry> entries{};
    for (const auto &file : io::GetEntries(cons::PROC_TABLE_DIR))
    {
        if (!io::IsRegularFile(file))
            continue;
        std::string content = io::ReadAllText(file);
        entries[file] = app::ProcTableEntry::Decode(content);
    }

    uint16_t found = 0;
    skippedDueToVersion = 0;

    for (auto &e : entries)
    {
        // If no such process, it means that this ProcTable file is outdated
        // Therefore that file should be deleted
        if (processes.count(e.second.pid) == 0)
        {
            io::Remove(e.first);
            continue;
        }

        // If searching node exists in this file, extract port number from it.
        for (auto &n : e.second.nodes)
        {
            if (n == node)
            {
                if (e.second.major == cons::Major && e.second.minor == cons::Minor && e.second.patch == cons::Patch)
                    found = e.second.port;
                else
                    skippedDueToVersion++;
            }
        }
    }

    return found;
}

static std::vector<std::string> DumpNames()
{
    std::vector<std::string> v{};

    if (!io::Exists(cons::PROC_TABLE_DIR))
        return v;

    // Find all processes in the environment
    auto processes = FindProcesses();

    // Read and parse ProcTable entries.
    std::unordered_map<std::string, app::ProcTableEntry> entries{};
    for (const auto &file : io::GetEntries(cons::PROC_TABLE_DIR))
    {
        if (!io::IsRegularFile(file))
            continue;
        std::string content = io::ReadAllText(file);
        entries[file] = app::ProcTableEntry::Decode(content);
    }

    for (auto &e : entries)
    {
        // If no such process, it means that this ProcTable file is outdated
        // Therefore that file should be deleted
        if (processes.count(e.second.pid) == 0)
        {
            io::Remove(e.first);
            continue;
        }

        for (auto &n : e.second.nodes)
            v.push_back(n);
    }

    std::sort(v.begin(), v.end());
    return v;
}

static void ReadOptions(int argc, char **argv)
{
    opt::OptionsDescription desc{
        "UERANSIM",
        cons::Tag,
        "Command Line Interface",
        cons::Owner,
        "nr-cli",
        {"<node-name> [option...]", "--dump", "--mass-dereg <cause> [--nodes <file>]"},
        {},
        true,
        false};

    opt::OptionItem itemDump      = {'d', "dump",      "List all UE and gNBs in the environment",                          std::nullopt};
    opt::OptionItem itemExec      = {'e', "exec",      "Execute the given command directly without an interactive shell",   "command"};
    opt::OptionItem itemMassDereg = {std::nullopt, "mass-dereg", "Deregister all (or listed) UEs in parallel",              "cause"};
    opt::OptionItem itemNodes     = {std::nullopt, "nodes",      "File with one UE node name per line (default: all UEs)",  "file"};

    desc.items.push_back(itemDump);
    desc.items.push_back(itemExec);
    desc.items.push_back(itemMassDereg);
    desc.items.push_back(itemNodes);

    opt::OptionsResult opt{argc, argv, desc, false, nullptr};

    g_options.dumpNodes = opt.hasFlag(itemDump);

    if (opt.hasFlag(itemMassDereg))
    {
        g_options.massDeregister = true;
        g_options.massDeregCause = opt.getOption(itemMassDereg);
        g_options.massDeregFile  = opt.getOption(itemNodes);

        static const std::vector<std::string> validCauses = {"normal", "disable-5g", "switch-off", "remove-sim"};
        if (std::find(validCauses.begin(), validCauses.end(), g_options.massDeregCause) == validCauses.end())
        {
            opt.showError("Invalid cause '" + g_options.massDeregCause +
                          "'. Expected: normal | disable-5g | switch-off | remove-sim");
        }
        return;
    }

    if (!g_options.dumpNodes)
    {
        if (opt.positionalCount() == 0)
        {
            opt.showError("Node name is expected");
            return;
        }
        if (opt.positionalCount() > 1 && !opt.hasFlag(itemExec))
        {
            opt.showError("Only one node name is expected");
            return;
        }

        g_options.nodeName = opt.getPositional(0);
        if (g_options.nodeName.size() < cons::MinNodeName)
        {
            opt.showError("Node name is too short");
            return;
        }
        if (g_options.nodeName.size() > cons::MaxNodeName)
        {
            opt.showError("Node name is too long");
            return;
        }

        g_options.directCmd = opt.getOption(itemExec);
        // append any extra positionals (e.g. "deregister normal") to the command
        for (int i = 1; i < opt.positionalCount(); i++)
            g_options.directCmd += " " + opt.getPositional(i);
        if (opt.hasFlag(itemExec) && g_options.directCmd.size() < 3)
        {
            opt.showError("Command is too short");
            return;
        }
    }
}

static bool HandleMessage(const app::CliMessage &msg, bool isOneShot)
{
    if (msg.type == app::CliMessage::Type::ERROR)
    {
        std::cerr << "ERROR: " << msg.value << std::endl;
        if (isOneShot)
            exit(1);
        return true;
    }

    if (msg.type == app::CliMessage::Type::ECHO)
    {
        std::cout << msg.value << std::endl;
        return true;
    }

    if (msg.type == app::CliMessage::Type::RESULT)
    {
        std::cout << msg.value << std::endl;
        if (isOneShot)
            exit(0);
        return true;
    }

    return false;
}

[[noreturn]] static void SendCommand(uint16_t port)
{
    app::CliServer server{};

    if (g_options.directCmd.empty())
    {
        while (true)
        {
            std::cout << "\x1b[1m";
            std::cout << std::string(92, '-') << std::endl;
            std::string line{};
            bool isEof{};
            std::vector<std::string> tokens{};
            if (!opt::ReadLine(std::cin, std::cout, line, tokens, isEof))
            {
                if (isEof)
                    exit(0);
                else
                    std::cout << "ERROR: Invalid command" << std::endl;
            }
            std::cout << "\x1b[0m";
            if (line.empty())
                continue;

            server.sendMessage(
                app::CliMessage::Command(InetAddress{cons::CMD_SERVER_IP, port}, line, g_options.nodeName));

            while (!HandleMessage(server.receiveMessage(), false))
            {
                // empty
            }
        }
    }
    else
    {
        server.sendMessage(
            app::CliMessage::Command(InetAddress{cons::CMD_SERVER_IP, port}, g_options.directCmd, g_options.nodeName));

        while (true)
            HandleMessage(server.receiveMessage(), true);
    }
}

// Returns true on success, false on failure. Thread-safe: each call owns its own CliServer.
static bool SendOneCommand(const std::string &nodeName, const std::string &cmd)
{
    int skippedDueToVersion{};
    uint16_t port{};
    try
    {
        port = DiscoverNode(nodeName, skippedDueToVersion);
    }
    catch (const std::runtime_error &)
    {
        return false;
    }

    if (port == 0)
        return false;

    app::CliServer server{};
    server.sendMessage(app::CliMessage::Command(InetAddress{cons::CMD_SERVER_IP, port}, cmd, nodeName));

    while (true)
    {
        auto msg = server.receiveMessage();
        if (msg.type == app::CliMessage::Type::RESULT || msg.type == app::CliMessage::Type::ERROR)
            return msg.type == app::CliMessage::Type::RESULT;
    }
}

static std::vector<std::string> ReadNodeFile(const std::string &path)
{
    std::vector<std::string> nodes{};
    std::ifstream f{path};
    if (!f.is_open())
    {
        std::cerr << "ERROR: Cannot open node file: " << path << std::endl;
        exit(1);
    }
    std::string line{};
    while (std::getline(f, line))
    {
        // strip trailing whitespace / CR
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty())
            nodes.push_back(line);
    }
    return nodes;
}

static void MassDeregister(const std::string &cause, const std::string &nodesFile)
{
    std::vector<std::string> targets{};

    if (nodesFile.empty())
    {
        // discover all running UEs (filter to UE node names only)
        for (auto &n : DumpNames())
        {
            if (n.rfind("imsi-", 0) == 0 || n.rfind("imei-", 0) == 0 || n.rfind("imeisv-", 0) == 0)
                targets.push_back(n);
        }
        if (targets.empty())
        {
            std::cerr << "ERROR: No running UE nodes found" << std::endl;
            exit(1);
        }
    }
    else
    {
        targets = ReadNodeFile(nodesFile);
    }

    std::cout << "Deregistering " << targets.size() << " UE(s) with cause '" << cause << "'..." << std::endl;

    std::vector<std::thread> threads{};
    std::mutex printMtx{};
    int successCount{};
    int failCount{};
    std::mutex counterMtx{};

    const std::string cmd = "deregister " + cause;

    for (auto &node : targets)
    {
        threads.emplace_back([&, node]() {
            bool ok = SendOneCommand(node, cmd);
            {
                std::lock_guard<std::mutex> lk(printMtx);
                if (ok)
                    std::cout << "[OK]   " << node << std::endl;
                else
                    std::cout << "[FAIL] " << node << std::endl;
            }
            {
                std::lock_guard<std::mutex> lk(counterMtx);
                if (ok) successCount++; else failCount++;
            }
        });
    }

    for (auto &t : threads)
        t.join();

    std::cout << "\nDone: " << successCount << " succeeded, " << failCount << " failed." << std::endl;
}

int main(int argc, char **argv)
{
    ReadOptions(argc, argv);

    if (g_options.massDeregister)
    {
        MassDeregister(g_options.massDeregCause, g_options.massDeregFile);
        return 0;
    }

    // NOTE: This does not guarantee showing the exact realtime status.
    if (g_options.dumpNodes)
    {
        for (auto &n : DumpNames())
            std::cout << n << "\n";
        std::cout.flush();
        exit(0);
    }

    if (g_options.nodeName.empty())
    {
        std::cerr << "ERROR: No node name is specified" << std::endl;
        exit(1);
    }

    if (g_options.nodeName.size() > cons::MaxNodeName)
    {
        std::cerr << "ERROR: Node name is too long" << std::endl;
        exit(1);
    }

    if (g_options.nodeName.size() < cons::MinNodeName)
    {
        std::cerr << "ERROR: Node name is too short" << std::endl;
        exit(1);
    }

    uint16_t cmdPort{};
    int skippedDueToVersion{};

    try
    {
        cmdPort = DiscoverNode(g_options.nodeName, skippedDueToVersion);
    }
    catch (const std::runtime_error &e)
    {
        throw std::runtime_error("Node discovery failure: " + std::string{e.what()});
    }

    if (cmdPort == 0)
    {
        std::cerr << "ERROR: No node found with name: " << g_options.nodeName << std::endl;
        if (skippedDueToVersion > 0)
            std::cerr << "WARNING: " << skippedDueToVersion
                      << " node(s) skipped due to version mismatch between the node and the CLI" << std::endl;
        return 1;
    }

    SendCommand(cmdPort);
    return 0;
}