#pragma once
#include <string>
#include "../../common/src/filesystem/filemanager.hpp"
#include "client.hpp"
#include <atomic>
namespace SquidStorage
{
    void attachClient(Client client);
    void runClient();
    void RenderUI();
    void renderFileExplorer();
    void renderFileEditor();
    bool fileCanBeSaved();
}
