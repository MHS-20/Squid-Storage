#pragma once
#include <string>
#include "../../common/src/filesystem/filemanager.hpp"
#include "client.hpp"
namespace SquidStorage
{
    void runClient();
    void RenderUI();
    void renderFileExplorer();
    void renderFileEditor();
    bool fileCanBeSaved();
}
