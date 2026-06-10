#pragma once

#include "CliState.h"

/// @brief A simple CLI interface rendered using ImGui.
class CliUi
{
public:
    CliUi() = default;
    ~CliUi() = default;
    CliUi(const CliUi&) = delete;
    CliUi& operator=(const CliUi&) = delete;
    CliUi(CliUi&&) = default;
    CliUi& operator=(CliUi&&) = default;

    void Render(const char* panelName);

private:
    CliState m_State;
    bool m_ScrollToBottom{ false };
};