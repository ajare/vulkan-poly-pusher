#pragma once

#include <cstdint>

#include "imgui/imgui.h"


struct ImGuiBackendData
{
    void* mouseCursors[ImGuiMouseCursor_COUNT];
    void* mouseLastCursor;

    char* clipboardTextData;

    uint64_t time;

    int mouseButtonsDown;
    int mouseLastLeaveFrame;
};

