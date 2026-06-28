#include "CliUi.h"

#include <imgui.h>

namespace
{
int
CliInputCallback(ImGuiInputTextCallbackData* data)
{
    auto* state = static_cast<CliState*>(data->UserData);

    if(data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
    {
        return 0;
    }

    std::string replacement;

    if(data->EventKey == ImGuiKey_UpArrow)
    {
        replacement = state->HistoryBack();
    }
    else if(data->EventKey == ImGuiKey_DownArrow)
    {
        replacement = state->HistoryForward();
    }

    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement.c_str());

    return 0;
}
} // namespace

void
CliUi::Render(const char* panelName)
{
    ImGui::Begin(panelName);

    const float input_height = ImGui::GetFrameHeightWithSpacing();

    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild("CliScrollback",
        ImVec2(0.0f, -(input_height + spacing)),
        false,
        ImGuiWindowFlags_HorizontalScrollbar);

    for(const std::string& line : m_State.GetLines())
    {
        ImGui::TextUnformatted(line.c_str());
    }

    if(m_ScrollToBottom)
    {
        ImGui::SetScrollHereY(1.0f);
        m_ScrollToBottom = false;
    }

    ImGui::EndChild();

    ImGui::Separator();

    ImGui::TextUnformatted(">");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1.0f);

    const bool submitted = ImGui::InputText("##CliInput",
        m_State.GetInput().data(),
        m_State.GetInput().size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
        CliInputCallback,
        &m_State);

    ImGui::SetItemDefaultFocus();

    if(submitted)
    {
        if(m_State.GetInput()[0] != '\0')
        {
            const std::string command = m_State.GetInput().data();

            m_State.AddLine("> " + command);
            m_State.AddHistory(command);
            m_State.ClearInput();
            m_ScrollToBottom = true;

            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
}