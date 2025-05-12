#include <raylib.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "filemanager.hpp"

int main()
{
    const std::string FOLDER_PATH = "./data";
    constexpr int screenWidth = 800;
    constexpr int screenHeight = 450;
    FileManager fileManager;
    std::vector<std::string> files = fileManager.getFiles(FOLDER_PATH);
    const char *formattedFiles = fileManager.stringToChar(fileManager.formatFileList(files));
    std::cout << formattedFiles << std::endl;
    InitWindow(screenWidth, screenHeight, "Squid Storage");
    // layout_name: controls initialization
    //----------------------------------------------------------------------------------
    int startIndex = 0;
    int selectedIndex = 0;
    bool newFileButtonPressed = false;
    bool openButtonPressed = false;
    bool deleteButtonPressed = false;
    bool showFileNameInput = false;
    bool showFileContent = false;
    char newFileName[128] = "file.txt";
    char *fileContentChar;
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
            showFileNameInput = true;
        }
        if (showFileNameInput)
        {
            bool editMode = true;

            if (GuiTextBox((Rectangle){432, 104, 120, 24}, newFileName, 128, editMode))
            {
                std::string newFilePath = FOLDER_PATH + "/" + std::string(newFileName);
                fileManager.createFile(newFilePath);
                files = fileManager.getFiles(FOLDER_PATH);
                formattedFiles = fileManager.stringToChar(fileManager.formatFileList(files));
                showFileNameInput = false;
                strcpy(newFileName, "file.txt");
            }
        }

        if (openButtonPressed)
        {
            showFileContent = true;
            std::string fileContent = fileManager.readFile(files[selectedIndex]);
            fileContentChar = fileManager.stringToChar(fileContent);
        }
        if (showFileContent)
        {
            if (GuiTextBoxMultiline((Rectangle){8, 8, 400, 200}, fileContentChar, 1024, true))
            {
                std::string newContent = fileContentChar;
                fileManager.updateFile(files[selectedIndex], newContent);
                showFileContent = false;
            }
        }
        if (deleteButtonPressed)
        {
            std::string selectedFile = files[selectedIndex];
            fileManager.deleteFile(selectedFile);
            files = fileManager.getFiles(FOLDER_PATH);
            formattedFiles = fileManager.stringToChar(fileManager.formatFileList(files));
        }

        EndDrawing();
    }

    CloseWindow();
}