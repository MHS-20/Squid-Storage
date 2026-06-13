#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "filemanager.hpp"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// EpochStore — reads and writes the leadership epoch to
//   <storageRoot>/.squid/epoch.txt
//
// The epoch is a uint32 that increments every time leadership changes.
// It is written to disk *before* the promoting server starts accepting
// connections, so that if it crashes and restarts it remembers it is no longer
// at epoch 0.
// ─────────────────────────────────────────────────────────────────────────────
class EpochStore {
public:
    explicit EpochStore(FileManager &fm) : fm_(fm) {}

    uint32_t load() const {
        std::ifstream in(path());
        if (!in) return 0;
        uint32_t v = 0;
        in >> v;
        return v;
    }

    void save(uint32_t epoch) const {
        fs::path p = path();
        fs::path tmp = p;
        tmp += ".tmp";
        try {
            fs::create_directories(p.parent_path());
            {
                std::ofstream out(tmp, std::ios::trunc);
                if (!out) {
                    std::cerr << "[EpochStore]: cannot write " << tmp << "\n";
                    return;
                }
                out << epoch << "\n";
                out.flush();
            }
            // fsync before rename to ensure data is on stable storage.
            int fd = ::open(tmp.c_str(), O_RDONLY);
            if (fd >= 0) { ::fsync(fd); ::close(fd); }
            fs::rename(tmp, p);
        } catch (const std::exception &e) {
            std::cerr << "[EpochStore]: save failed: " << e.what() << "\n";
            std::error_code ec;
            fs::remove(tmp, ec);
        }
    }

private:
    FileManager &fm_;

    fs::path path() const {
        return FileManager::storageRoot() / ".squid" / "epoch.txt";
    }
};
