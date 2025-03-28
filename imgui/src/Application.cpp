﻿#include "Application.hpp"
#include "../imgui.h"
#include <iostream>

namespace SquidStorage
{

    std::string currentPath = fs::current_path().string(); // current directory
    std::string selectedFile = "";                         // selected file
    std::string fileContent = "";                          // selected file content
    FileManager fm = FileManager();
    bool showFileSavedMessage = false;
    bool showFileDeleteButton = false;
    bool showFileEditor = false;
    bool newFileButtonPressed = false;
    bool deleteButtonPressed = false;
    char newFileName[128];
    void RenderUI()
    {
        static bool opt_fullscreen = true;
        static bool opt_padding = false;

        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", nullptr, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // Submit the DockSpace
        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Options"))
            {
                // Disabling fullscreen would allow the window to be moved to the front of other windows,
                // which we can't undo at the moment without finer window depth/z control.
                ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen);
                ImGui::MenuItem("Padding", NULL, &opt_padding);
                ImGui::Separator();

                if (ImGui::MenuItem("Flag: NoDockingOverCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingOverCentralNode) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoDockingOverCentralNode;
                }
                if (ImGui::MenuItem("Flag: NoDockingSplit", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingSplit) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoDockingSplit;
                }
                if (ImGui::MenuItem("Flag: NoUndocking", "", (dockspace_flags & ImGuiDockNodeFlags_NoUndocking) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoUndocking;
                }
                if (ImGui::MenuItem("Flag: NoResize", "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_NoResize;
                }
                if (ImGui::MenuItem("Flag: AutoHideTabBar", "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar;
                }
                if (ImGui::MenuItem("Flag: PassthruCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) != 0, opt_fullscreen))
                {
                    dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode;
                }
                ImGui::Separator();
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        renderFileExplorer();
        renderFileEditor();
        ImGui::End();
    }

    void renderFileExplorer()
    {

        ImGui::Begin("File Explorer");

        if (ImGui::Button("Go back"))
        {
            currentPath = fs::path(currentPath).parent_path().string();
        }

        ImGui::Text("Current Path: %s", currentPath.c_str());

        // frame start
        ImGui::BeginChild("FileListFrame", ImVec2(0, 450), true);

        // files and directories list
        std::vector<fs::directory_entry> entries = fm.getFileEntries(currentPath);

        for (const auto &entry : entries)
        {
            const std::string name = entry.path().filename().string();
            bool isDir = entry.is_directory();

            if (isDir)
            {
                if (ImGui::Selectable(("+ " + name).c_str(), false))
                {
                    currentPath = entry.path().string();
                }
            }
            else
            {
                if (ImGui::Selectable(("- " + name).c_str(), selectedFile == name))
                {
                    selectedFile = entry.path().string();
                    fileContent = fm.readFile(selectedFile);
                    showFileDeleteButton = true;
                    showFileEditor = true;
                }
            }
        }
        // frame end
        ImGui::EndChild();

        // show selected file
        ImGui::Text("Selected File: %s", selectedFile.c_str());

        if (ImGui::Button("New file"))
        {
            newFileButtonPressed = true;
        }

        if (newFileButtonPressed)
        {

            ImGui::InputText("File name", newFileName, 128);
            if (ImGui::Button("Create"))
            {
                if (fm.createFile(newFileName))
                {
                    fileContent = "";
                    newFileButtonPressed = false;
                }
            }
        }
        ImGui::SameLine();
        if (showFileDeleteButton)
            if (ImGui::Button("Delete"))
                deleteButtonPressed = true;

        if (deleteButtonPressed)
        {
            if (fm.deleteFile(selectedFile))
            {
                selectedFile = "";
                fileContent = "";
                deleteButtonPressed = false;
            }
            showFileDeleteButton = false;
        }
        ImGui::End();
    }

    void renderFileEditor()
    {
        ImGui::Begin("File editor");
        static char buffer[4096];

        strncpy(buffer, fileContent.c_str(), sizeof(buffer));
        if (showFileEditor)
        {

            ImGui::BeginChild("FileEditorFrame", ImVec2(0, 500), true);

            if (ImGui::InputTextMultiline("##editor", buffer, sizeof(buffer), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 35)))
            {
                fileContent = std::string(buffer);
                showFileSavedMessage = false;
            }
            ImGui::EndChild();

            if (ImGui::Button("Save"))
            {
                if (fm.updateFile(selectedFile, fileContent))
                {
                    showFileSavedMessage = true;
                }
                else
                {
                    showFileSavedMessage = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                showFileEditor = false;
            }

            if (showFileSavedMessage)
                ImGui::Text("File saved!");
        }
        ImGui::End();
    }
}
