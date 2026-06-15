#include "DevUi.h"

#include "CliUi.h"
#include "PerfMetrics.h"
#include "Renderer.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>

namespace
{
float
GetStatusBarHeight()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + (style.WindowPadding.y * 2.0f);
}
} // namespace

Result<>
DevUi::Render()
{
    DrawDockedEditorLayout();

    DrawPerfPanel();
    DrawScenePanel();
    DrawConsolePanel();
    DrawStatusBarPanel();

    return Result<>::Ok;
}

// private:

void
DevUi::DrawDockedEditorLayout() const // NOLINT(readability-convert-member-functions-to-static)
{
    const ImGuiID dockspaceId = ImGui::GetID("MainDockspace");

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Dockspace should fill the entire viewport, except for space at the bottom for the status bar.
    ImVec2 workSize = viewport->WorkSize;
    workSize.y -= GetStatusBarHeight();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(workSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("Dockspace Host", nullptr, hostFlags);

    const ImGuiDockNodeFlags dockspaceFlags =
        ImGuiDockNodeFlags_None;
        //ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    static bool dockspaceBuilt = false;

    if (!dockspaceBuilt)
    {
        dockspaceBuilt = true;

        ImGui::DockBuilderRemoveNode(dockspaceId);

        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMainId = dockspaceId;

        const ImGuiID dockLeftId =
            ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);

        const ImGuiID dockBottomId =
            ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);

        ImGui::DockBuilderDockWindow(kScenePanelName, dockMainId);
        ImGui::DockBuilderDockWindow(kPerfPanelName, dockLeftId);
        ImGui::DockBuilderDockWindow(kConsolePanelName, dockBottomId);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void
DevUi::DrawPerfPanel() const // NOLINT(readability-convert-member-functions-to-static)
{
    constexpr size_t kMaxPerfStats = 256;

    PerfStats perfStats[kMaxPerfStats];
    std::span<PerfStats> perfStatsSpan(perfStats);

    // Timers
    size_t counterCount = PerfMetrics::SampleCounters<PerfTimerCategory>(perfStatsSpan);

    std::span<PerfStats> sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters,
        [](const PerfStats& a, const PerfStats& b)
        {
            return a.GetName() < b.GetName();
        });

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(kPerfPanelName);
    MLG_DEFER { ImGui::End(); };

    auto drawSubTree = [](this auto&& self, const std::string_view prefix, const std::span<PerfStats> pss) -> std::span<PerfStats>
    {
        bool isOpen = false;
        if(!prefix.empty())
        {
            // Prefix is non empty - render a tree node.
            const std::string prefixStr(prefix.data(), prefix.size());
            isOpen = ImGui::TreeNode(prefixStr.c_str());
        }

        MLG_DEFER
        {
            if(isOpen)
            {
                // Pop only if the tree node is open
                ImGui::TreePop();
            }
        };

        std::span<PerfStats> curPss = pss;

        while(!curPss.empty())
        {
            const PerfStats& ps = curPss.front();
            const std::string_view curName = ps.GetName();

            MLG_ASSERT(!curName.empty(), "Empty perf counter name");

            if(!curName.starts_with(prefix))
            {
                // Entering a new prefix - return to caller.
                return curPss;
            }

            if(prefix.empty())
            {
                // First time through prefix is empty.
                // Get the prefix and recurse.
                const size_t pos = curName.find_first_of('.');
                const std::string_view nextPrefix = curName.substr(0, pos);
                curPss = self(nextPrefix, curPss);
            }
            else if(!isOpen)
            {
                // Current node is not open.  No need to process subnodes.

                // Advance to the next node in the list.
                curPss = curPss.subspan(1);
            }
            else if(curName.size() >= prefix.size())
            {
                const size_t pos = curName.find_first_of('.', prefix.size() + 1);

                if(pos == std::string_view::npos)
                {
                    // Reached the leaf node.
                    const std::string_view leafName =
                    curName == prefix 
                    ? curName
                    : curName.substr(prefix.size() + 1, pos);
                    const std::string text = std::format("{}: {:.3f}", leafName, ps.GetEMA());
                    ImGui::TreeNodeEx(text.c_str(),
                        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

                    // Advance to the next node in the list.
                    curPss = curPss.subspan(1);
                }
                else
                {
                    // Recurse to the next subnode.
                    const std::string_view nextPrefix = curName.substr(0, pos);
                    curPss = self(nextPrefix, curPss);
                }
            }
        }

        return curPss;
    };

    drawSubTree("", sortedCounters);

    // Other counters
    counterCount = PerfMetrics::SampleCounters<>(perfStatsSpan);

    sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters,
        [](const PerfStats& a, const PerfStats& b)
        {
            return a.GetName() < b.GetName();
        });

    drawSubTree("", sortedCounters);
}

void
DevUi::DrawScenePanel()
{
    //ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 0.0f);
    ImGui::Begin(kScenePanelName, nullptr, ImGuiWindowFlags_NoBackground);

    wgpu::Texture texture;
    wgpu::TextureView textureView;
    m_Renderer->GetTarget(texture, textureView);
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 mousePos = ImGui::GetMousePos();

    m_ScenePanelRect = Rect //
        {
            .X = static_cast<int>(cursorPos.x),
            .Y = static_cast<int>(cursorPos.y),
            .Width = static_cast<unsigned>(avail.x),
            .Height = static_cast<unsigned>(avail.y),
        };

    m_ScenePanelMousePos.X = static_cast<int>(mousePos.x - cursorPos.x);
    m_ScenePanelMousePos.Y = static_cast<int>(mousePos.y - cursorPos.y);

    ImGui::Image(ImTextureRef(textureView.Get()), avail);
    
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void DevUi::DrawConsolePanel() const // NOLINT(readability-convert-member-functions-to-static)
{
    m_CliUi->Render(kConsolePanelName);
}

void DevUi::DrawStatusBarPanel() const // NOLINT(readability-convert-member-functions-to-static)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float statusBarHeight = GetStatusBarHeight();

    // Position the status bar at the bottom of the viewport, spanning its entire width
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::Begin(kStatusBarPanelName,
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar);

    MLG_DEFER { ImGui::End(); };

    const std::string statusText = std::format("SPF: {:.3f} ms | FPS: {:.1f} | mouse: {},{}",
        ImGui::GetIO().DeltaTime * 1000.0f,
        1.0f / ImGui::GetIO().DeltaTime,
        m_ScenePanelMousePos.X,
        m_ScenePanelMousePos.Y);

    ImGui::TextUnformatted(statusText.c_str());
}