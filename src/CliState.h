#pragma once

#include <string>
#include <vector>
#include <span>
#include <array>
class CliState
{
public:
    static constexpr size_t kMaxInputChars = 1024;

    std::span<char> GetInput() { return m_Input; }

    void ClearInput()
    {
        m_Input[0] = '\0';
        m_PendingInput.clear();
    }

    void AddLine(std::string line)
    {
        m_Lines.emplace_back(std::move(line));
    }

    void AddHistory(std::string command)
    {
        if(m_History.empty() || m_History.back() != command)
        {
            m_History.emplace_back(std::move(command));
            m_HistoryIt = m_History.end();
        }
    }

    /// @brief Moves the history pointer back and returns the command at the new position.
    const std::string& HistoryBack()
    {
        static const std::string emptyString;

        if(m_History.empty())
        {
            return emptyString;
        }

        if(m_HistoryIt == m_History.end())
        {
            m_PendingInput.assign(m_Input.data(), static_cast<size_t>(strlen(m_Input.data())));
        }

        if(m_HistoryIt != m_History.begin())
        {
            --m_HistoryIt;
        }

        return *m_HistoryIt;
    }

    /// @brief Moves the history pointer forward and returns the command at the new position.
    const std::string& HistoryForward()
    {
        static const std::string emptyString;

        if(!m_History.empty() && m_HistoryIt != m_History.end())
        {
            ++m_HistoryIt;
        }

        if(m_HistoryIt == m_History.end())
        {
            return m_PendingInput;
        }

        return *m_HistoryIt;
    }

    std::span<const std::string> GetLines() const { return m_Lines; }
    std::span<const std::string> GetHistory() const { return m_History; }

private:
    std::vector<std::string> m_Lines;
    std::vector<std::string> m_History;
    std::array<char, kMaxInputChars> m_Input = {};

    std::vector<std::string>::iterator m_HistoryIt = m_History.end();

    std::string m_PendingInput; // Input restored after navigating back past newest history item.
};