#include <raylib.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <iostream>
#include <string>
#include <vector>
#include "filemanager.hpp"

int main()
{
    const Color darkGreen = {20, 160, 133, 255};

    constexpr int screenWidth = 800;
    constexpr int screenHeight = 450;
    FileManager fileManager;

    fileManager.getFiles(".");

    InitWindow(screenWidth, screenHeight, "Squid Storage");
    // layout_name: controls initialization
    //----------------------------------------------------------------------------------
    int startIndex = 0;
    int selectedIndex = 0;
    bool Button002Pressed = false;
    bool Button003Pressed = false;
    bool Button004Pressed = false;
    //----------------------------------------------------------------------------------

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {

        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        // raygui: controls drawing
        //----------------------------------------------------------------------------------
        GuiListView((Rectangle){8, 8, 392, 496}, "ONE;TWO;THREE", &startIndex, &selectedIndex);
        Button002Pressed = GuiButton((Rectangle){432, 8, 120, 24}, "New File");
        Button003Pressed = GuiButton((Rectangle){432, 40, 120, 24}, "Open");
        Button004Pressed = GuiButton((Rectangle){432, 72, 120, 24}, "Delete");
        //----------------------------------------------------------------------------------

        EndDrawing();
    }

    CloseWindow();
}