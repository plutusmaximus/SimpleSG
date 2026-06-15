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

    const Rect& GetScenePanelRect() const { return m_ScenePanelRect; }

    const Point& GetScenePanelMousePos() const { return m_ScenePanelMousePos; }

private:

    constexpr static const char* kScenePanelName = "Scene";
    constexpr static const char* kPerfPanelName = "Performance";
    constexpr static const char* kConsolePanelName = "Console";
    constexpr static const char* kStatusBarPanelName = "StatusBar";

    void DrawPerfPanel() const;

    void DrawScenePanel();

    void DrawConsolePanel() const;

    void DrawStatusBarPanel() const;

    void DrawDockedEditorLayout() const;

    Rect m_ScenePanelRect{.X = 0, .Y = 0, .Width = 0, .Height = 0};

    Point m_ScenePanelMousePos{.X = 0, .Y = 0};

    const Renderer* m_Renderer;
    CliUi* m_CliUi;
};