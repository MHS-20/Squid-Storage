#include <raylib.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "filemanager.hpp"

std::string formatFileList(std::vector<std::string> files)
{
    std::string fileList = "";
    for (auto file : files)
    {
        fileList += file + ";";
    }
    fileList.pop_back();
    return fileList;
}

int main()
{
    const std::string FOLDER_PATH = "./data";
    constexpr int screenWidth = 800;
    constexpr int screenHeight = 450;
    FileManager fileManager;
    std::vector<std::string> files = fileManager.getFiles(FOLDER_PATH);
    const char *formattedFiles = fileManager.stringToChar(formatFileList(files));
    std::cout << formattedFiles << std::endl;
    InitWindow(screenWidth, screenHeight, "Squid Storage");
    // layout_name: controls initialization
    //----------------------------------------------------------------------------------
    int startIndex = 0;
    int selectedIndex = 0;
    bool newFileButtonPressed = false;
    bool openButtonPressed = false;
    bool deleteButtonPressed = false;
    //----------------------------------------------------------------------------------

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {

        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        // raygui: controls drawing
        //----------------------------------------------------------------------------------
        GuiListView((Rectangle){8, 8, 392, 496}, formattedFiles, &startIndex, &selectedIndex);
        newFileButtonPressed = GuiButton((Rectangle){432, 8, 120, 24}, "New File");
        openButtonPressed = GuiButton((Rectangle){432, 40, 120, 24}, "Open");
        deleteButtonPressed = GuiButton((Rectangle){432, 72, 120, 24}, "Delete");
        //----------------------------------------------------------------------------------
        if (newFileButtonPressed)
        {
            char newFileName[128] = "file.txt";
            bool editMode = true;

            GuiTextBox((Rectangle){432, 104, 120, 24}, newFileName, 128, editMode);

            std::string newFilePath = FOLDER_PATH + "/" + std::string(newFileName);
            fileManager.createFile(newFilePath);
            files = fileManager.getFiles(FOLDER_PATH);
            formattedFiles = fileManager.stringToChar(formatFileList(files));
        }

        if (openButtonPressed)
        {
            std::string selectedFile = files[selectedIndex];
            std::ifstream file(selectedFile);
            std::string fileContent;
            std::string line;
            while (std::getline(file, line))
            {
                fileContent += line + "\n";
            }
            file.close();
            char *fileContentChar = fileManager.stringToChar(fileContent);
            // GuiTextBoxMulti((Rectangle){8, 8, 392, 496}, fileContentChar, 1024, true);
        }
        if (deleteButtonPressed)
        {
            std::string selectedFile = files[selectedIndex];
            fileManager.deleteFile(selectedFile);
            files = fileManager.getFiles(FOLDER_PATH);
            formattedFiles = fileManager.stringToChar(formatFileList(files));
        }

        EndDrawing();
    }

    CloseWindow();
}