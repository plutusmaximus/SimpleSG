#pragma once

class CliUi;
class Renderer;

#include "Result.h"
#include "VecMath.h"

/// @brief Dev UI overlay.  Pass in the renderer for access to the render target
/// so it can be rendered in the scene panel.
class DevUi
{
public:
    explicit DevUi(CliUi& cliUi, const Renderer& renderer)
        : m_Renderer(&renderer),
          m_CliUi(&cliUi)
    {
    }

    Result<> Render();

    const Extent& GetScenePanelDimension() const { return m_ScenePanelDimension; }

    void SetMouseXY(const int x, const int y)
    {
        m_MouseX = x;
        m_MouseY = y;
    }

private:

    constexpr static const char* kScenePanelName = "Scene";
    constexpr static const char* kPerfPanelName = "Performance";
    constexpr static const char* kConsolePanelName = "Console";
    constexpr static const char* kStatusBarPanelName = "StatusBar";

    void DrawPerfPanel();

    void DrawScenePanel();

    void DrawConsolePanel();

    void DrawStatusBarPanel();

    void DrawDockedEditorLayout();

    Extent m_ScenePanelDimension{.Width = 0, .Height = 0};
    const Renderer* m_Renderer;
    CliUi* m_CliUi;
    int m_MouseX{0}, m_MouseY{0};
};