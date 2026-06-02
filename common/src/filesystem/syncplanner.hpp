#pragma once

#include <map>
#include <string>
#include <vector>

enum class SyncAction
{
    UPLOAD,
    DOWNLOAD,
    CREATE_REMOTE,
};

struct SyncOp
{
    SyncAction action;
    std::string filePath;
    int version;
};

inline std::vector<SyncOp> planSync(
    const std::map<std::string, int> &localMap,
    const std::map<std::string, int> &remoteMap)
{
    std::vector<SyncOp> ops;

    std::map<std::string, int> remote = remoteMap;

    for (const auto &local : localMap)
    {
        auto it = remote.find(local.first);
        if (it != remote.end())
        {
            if (local.second > it->second)
                ops.push_back({SyncAction::UPLOAD, local.first, local.second});
            else if (local.second < it->second)
                ops.push_back({SyncAction::DOWNLOAD, local.first, it->second});
            remote.erase(it);
        }
        else
        {
            ops.push_back({SyncAction::CREATE_REMOTE, local.first, local.second});
        }
    }

    for (const auto &r : remote)
        ops.push_back({SyncAction::DOWNLOAD, r.first, r.second});

    return ops;
}
