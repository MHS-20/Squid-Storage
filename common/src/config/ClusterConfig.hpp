#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ClusterConfig — parses a simple INI-style cluster config file.
//
// Example config file:
//
//   [servers]
//   server1 = 192.168.1.10:9000
//   server2 = 192.168.1.11:9000
//   server3 = 192.168.1.12:9000
//
//   [replication]
//   heartbeat_interval_ms   = 500
//   heartbeat_timeout_ms    = 1500
//   reconnect_attempts      = 3
//   reconnect_delay_ms      = 500
//   promotion_probe_attempts = 3
//   promotion_probe_delay_ms = 200
//
// The declaration order of servers determines priority:
//   first entry = highest priority (primary), last = lowest priority.
//
// All nodes (server, client, datanode) carry this same config so they can
// walk the server list in priority order for failover.
// ─────────────────────────────────────────────────────────────────────────────

struct ServerEntry {
    std::string name;   // e.g. "server1"
    std::string ip;     // e.g. "192.168.1.10"
    int         port;   // e.g. 9000
};

struct ClusterConfig {
    // Ordered list of servers, highest priority first.
    std::vector<ServerEntry> servers;

    int replication_factor = 2;

    // Timing parameters with defaults matching the design document.
    int heartbeat_interval_ms    = 500;
    int heartbeat_timeout_ms     = 1500;
    int reconnect_attempts       = 3;
    int reconnect_delay_ms       = 500;
    int promotion_probe_attempts = 3;
    int promotion_probe_delay_ms = 200;

    // ── Queries ───────────────────────────────────────────────────────────────

    // Returns 0-based priority of a server (0 = highest priority).
    // Returns -1 if not found.
    int priorityOf(const std::string &name) const {
        for (int i = 0; i < (int)servers.size(); ++i)
            if (servers[i].name == name) return i;
        return -1;
    }

    // Returns all ServerEntry objects with strictly higher priority than `name`.
    std::vector<ServerEntry> higherPriorityThan(const std::string &name) const {
        std::vector<ServerEntry> result;
        for (const auto &s : servers) {
            if (s.name == name) break;
            result.push_back(s);
        }
        return result;
    }

    // Find a server entry by name. Returns nullptr if not found.
    const ServerEntry* find(const std::string &name) const {
        for (const auto &s : servers)
            if (s.name == name) return &s;
        return nullptr;
    }

    // True if this config has at least one server defined.
    bool valid() const { return !servers.empty(); }

    // True if the given name is the primary (highest priority) server.
    bool isPrimary(const std::string &name) const {
        return !servers.empty() && servers[0].name == name;
    }

    // ── Parser ────────────────────────────────────────────────────────────────

    // Parses a config file. Throws std::runtime_error on fatal errors.
    // Lines beginning with '#' or ';' are comments. Blank lines are ignored.
    static ClusterConfig fromFile(const std::string &path) {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("ClusterConfig: cannot open '" + path + "'");

        ClusterConfig cfg;
        std::string currentSection;
        std::string line;
        int lineNum = 0;

        while (std::getline(in, line)) {
            ++lineNum;
            // Strip leading/trailing whitespace.
            auto ltrim = [](std::string &s) {
                s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                        [](unsigned char c){ return !std::isspace(c); }));
            };
            auto rtrim = [](std::string &s) {
                s.erase(std::find_if(s.rbegin(), s.rend(),
                        [](unsigned char c){ return !std::isspace(c); }).base(),
                        s.end());
            };
            ltrim(line); rtrim(line);

            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            // Section header.
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            // Key = value.
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key   = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            ltrim(key); rtrim(key);
            ltrim(value); rtrim(value);

            // Strip inline comments.
            auto semi = value.find(';');
            if (semi != std::string::npos) { value = value.substr(0, semi); rtrim(value); }

            if (currentSection == "servers") {
                // value is "ip:port"
                auto colon = value.rfind(':');
                if (colon == std::string::npos) {
                    std::cerr << "ClusterConfig: bad server entry on line "
                              << lineNum << ": '" << line << "'\n";
                    continue;
                }
                ServerEntry e;
                e.name = key;
                e.ip   = value.substr(0, colon);
                try { e.port = std::stoi(value.substr(colon + 1)); }
                catch (...) {
                    std::cerr << "ClusterConfig: bad port on line " << lineNum << "\n";
                    continue;
                }
                cfg.servers.push_back(e);
            }
            else if (currentSection == "replication") {
                auto parseInt = [&](int &dst) {
                    try { dst = std::stoi(value); }
                    catch (...) {
                        std::cerr << "ClusterConfig: bad integer value for '"
                                  << key << "' on line " << lineNum << "\n";
                    }
                };
                if      (key == "replication_factor")      parseInt(cfg.replication_factor);
                else if (key == "heartbeat_interval_ms")    parseInt(cfg.heartbeat_interval_ms);
                else if (key == "heartbeat_timeout_ms")     parseInt(cfg.heartbeat_timeout_ms);
                else if (key == "reconnect_attempts")       parseInt(cfg.reconnect_attempts);
                else if (key == "reconnect_delay_ms")       parseInt(cfg.reconnect_delay_ms);
                else if (key == "promotion_probe_attempts") parseInt(cfg.promotion_probe_attempts);
                else if (key == "promotion_probe_delay_ms") parseInt(cfg.promotion_probe_delay_ms);
                else
                    std::cerr << "ClusterConfig: unknown replication key '"
                              << key << "' on line " << lineNum << "\n";
            }
        }

        return cfg;
    }

    // Build a minimal single-server config for standalone (non-HA) mode.
    static ClusterConfig standalone(const std::string &ip, int port,
                                    const std::string &name = "server1") {
        ClusterConfig cfg;
        cfg.servers.push_back({name, ip, port});
        return cfg;
    }
};
