#include <raylib.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <iostream>
#include <string>
#include <vector>
#include "filemanager.hpp"

int main()
{

    constexpr int screenWidth = 800;
    constexpr int screenHeight = 450;
    // FileManager fileManager;

    // fileManager.getFiles(".");

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
        GuiListView((Rectangle){8, 8, 392, 496}, "ONE;TWO;THREE", &startIndex, &selectedIndex);
        newFileButtonPressed = GuiButton((Rectangle){432, 8, 120, 24}, "New File");
        openButtonPressed = GuiButton((Rectangle){432, 40, 120, 24}, "Open");
        deleteButtonPressed = GuiButton((Rectangle){432, 72, 120, 24}, "Delete");
        //----------------------------------------------------------------------------------

        EndDrawing();
    }

    CloseWindow();
}