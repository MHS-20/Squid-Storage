#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "filemanager.hpp"

namespace fs = std::filesystem;

// Persists server-side state (file version map and replication map) to
// .squid/fileVersions.txt and .squid/replicationMap.txt under the storage
// root.  All writes use an atomic rename(tmp, dst) to avoid partial state.
//
// Format – fileVersions.txt (one entry per line):
//   <relpath> <version>
//
// Format – replicationMap.txt (one entry per line):
//   <relpath> <datanodeName1>[,<datanodeName2>,...]
class StateManager {
public:
  explicit StateManager(FileManager &fileManager)
      : fileManager_(fileManager) {}

  // ── File-version state ────────────────────────────────────────────────────

  // Load the persisted version map (highest-version-wins reconciliation
  // against what datanodes report happens in ReplicationManager).
  std::map<std::string, int> loadVersionMap() const {
    std::map<std::string, int> result;
    std::ifstream in(versionPath());
    if (!in)
      return result;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#')
        continue;
      auto sp = line.rfind(' ');
      if (sp == std::string::npos)
        continue;
      std::string path = line.substr(0, sp);
      int version = 0;
      try { version = std::stoi(line.substr(sp + 1)); } catch (...) { continue; }
      result[path] = version;
    }
    return result;
  }

  void saveVersionMap(const std::map<std::string, int> &versionMap) const {
    atomicWrite(versionPath(), [&](std::ofstream &out) {
      for (auto &[path, ver] : versionMap)
        out << path << ' ' << ver << '\n';
    });
  }

  // ── Replication state ─────────────────────────────────────────────────────

  // Returns: filePath → set of datanodeNames that hold it.
  std::map<std::string, std::set<std::string>> loadReplicationMap() const {
    std::map<std::string, std::set<std::string>> result;
    std::ifstream in(replicationPath());
    if (!in)
      return result;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#')
        continue;
      auto sp = line.rfind(' ');
      if (sp == std::string::npos)
        continue;
      std::string path = line.substr(0, sp);
      std::string nodeList = line.substr(sp + 1);
      // parse comma-separated node names
      std::set<std::string> nodes;
      std::string tok;
      for (char c : nodeList) {
        if (c == ',') {
          if (!tok.empty()) { nodes.insert(tok); tok.clear(); }
        } else {
          tok += c;
        }
      }
      if (!tok.empty()) nodes.insert(tok);
      result[path] = std::move(nodes);
    }
    return result;
  }

  // filePath → set of datanodeNames
  void saveReplicationMap(
      const std::map<std::string, std::set<std::string>> &repMap) const {
    atomicWrite(replicationPath(), [&](std::ofstream &out) {
      for (auto &[path, nodes] : repMap) {
        out << path << ' ';
        bool first = true;
        for (auto &n : nodes) {
          if (!first) out << ',';
          out << n;
          first = false;
        }
        out << '\n';
      }
    });
  }

private:
  FileManager &fileManager_;

  fs::path versionPath() const {
    return FileManager::storageRoot() / ".squid" / "fileVersions.txt";
  }

  fs::path replicationPath() const {
    return FileManager::storageRoot() / ".squid" / "replicationMap.txt";
  }

  // Write fn(stream) to a tmp file then atomically rename it into dst.
  template <typename Fn>
  void atomicWrite(const fs::path &dst, Fn &&fn) const {
    fs::path tmp = dst;
    tmp += ".tmp";
    try {
      fs::create_directories(dst.parent_path());
      {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
          std::cerr << "[StateManager]: cannot open " << tmp << " for writing\n";
          return;
        }
        fn(out);
        out.flush();
      }
      // fsync before rename to ensure data is on stable storage.
      int fd = ::open(tmp.c_str(), O_RDONLY);
      if (fd >= 0) { ::fsync(fd); ::close(fd); }
      fs::rename(tmp, dst);
    } catch (const std::exception &e) {
      std::cerr << "[StateManager]: atomicWrite failed: " << e.what() << '\n';
      std::error_code ec;
      fs::remove(tmp, ec);
    }
  }
};
