#include "InputMapper.h"

#include <algorithm>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <span>
#include <variant>

namespace
{
bool
ValidateInputButton(const InputButton& button)
{
    switch(button.GetDevice())
    {
        case InputButtonDevice::Keyboard:
            return button.GetId() < SDL_SCANCODE_COUNT;

        case InputButtonDevice::Mouse:
            return button.GetId() < InputMapper::kMaxMouseButtons;
    }

    return false;
}
} // namespace

InputMapper::InputMapper(const std::span<const ActionMapping> mappings)
{
    // Count mappings so action arrays can be allocated.
    size_t buttonMappingCount = 0;
    size_t axisMappingCount = 0;
    for(const ActionMapping& mapping : mappings)
    {
        if(std::holds_alternative<InputButton>(mapping.Input))
        {
            ++buttonMappingCount;
        }
        else if(std::holds_alternative<InputAxis>(mapping.Input))
        {
            ++axisMappingCount;
        }
    }

    m_ButtonActionMappings.reserve(buttonMappingCount);
    m_AxisActionMappings.reserve(axisMappingCount);
    m_ActionStates.reserve(mappings.size());

    for(const ActionMapping& mapping : mappings)
    {
        m_ActionStates.push_back(ActionState{ .ActionId = mapping.ActionId });
    }

    // Sort action states and remove duplicates.
    std::ranges::sort(m_ActionStates, {}, &ActionState::ActionId);
    const auto dupRange = std::ranges::unique(m_ActionStates, {}, &ActionState::ActionId);
    m_ActionStates.erase(dupRange.begin(), dupRange.end());

    for(const ActionMapping& mapping : mappings)
    {
        // Input can be either InputButton or InputAxis.

        if(std::holds_alternative<InputButton>(mapping.Input))
        {
            const InputButton& button = std::get<InputButton>(mapping.Input);

            MLG_ABORTIF(!ValidateInputButton(button),
                "Invalid InputButton mapping: device={}, id={}",
                static_cast<int>(button.GetDevice()),
                button.GetId());

            ButtonActionMapping& bam = m_ButtonActionMappings.emplace_back(button, mapping.Scale);
            bam.m_ActionState = GetActionState(mapping.ActionId);
        }
        else if(std::holds_alternative<InputAxis>(mapping.Input))
        {
            const InputAxis& axis = std::get<InputAxis>(mapping.Input);

            AxisActionMapping& aam = m_AxisActionMappings.emplace_back(axis, mapping.Scale);
            aam.m_ActionState = GetActionState(mapping.ActionId);
        }
    }

    const size_t numKeyStates = static_cast<size_t>(SDL_SCANCODE_COUNT);
    m_KeyStates.resize(numKeyStates);
}

void
InputMapper::Clear()
{
    for(auto& actionState : m_ActionStates)
    {
        actionState.Triggered = false;
        actionState.Value = 0.0f;
    }

    for(auto& keyState : m_KeyStates)
    {
        keyState.PressCount = 0;
        keyState.ReleaseCount = 0;
        keyState.DownState = false;
    }

    for(auto& mouseButtonState : m_MouseButtonStates)
    {
        mouseButtonState.PressCount = 0;
        mouseButtonState.ReleaseCount = 0;
        mouseButtonState.DownState = false;
    }

    int numKeys = 0;
    const bool* keyboardState = SDL_GetKeyboardState(&numKeys);

    const size_t keyCount = static_cast<size_t>(numKeys);
    MLG_ASSERT(keyCount == m_KeyStates.size(), "SDL_GetKeyboardState() returned unexpected number of keys");

    const std::span<const bool> keyboardStateSpan(keyboardState, keyCount);

    for(size_t i = 0; i < keyCount; ++i)
    {
        m_KeyStates[i].DownState = keyboardStateSpan[i];
    }

    const SDL_MouseButtonFlags mouseButtonBits = SDL_GetMouseState(nullptr, nullptr);

    for(size_t i = 1; i < m_MouseButtonStates.size(); ++i)
    {
        const unsigned buttonMask = SDL_BUTTON_MASK(i);

        m_MouseButtonStates[i].DownState = (mouseButtonBits & buttonMask) != 0;
    }
}

void
InputMapper::BeginFrame()
{
    MLG_ASSERT(!m_InFrame, "BeginFrame() called without a matching EndFrame()");
    m_InFrame = true;

    for(auto& actionState : m_ActionStates)
    {
        actionState.Triggered = false;
        actionState.Value = 0.0f;
    }
}

void
InputMapper::ProcessEvent(const SDL_Event& event)
{
    MLG_ASSERT(m_InFrame, "ConsumeEvent() called outside of BeginFrame()/EndFrame()");

    switch(event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            if(event.type == SDL_EVENT_KEY_UP)
            {
                const unsigned scancode = static_cast<unsigned>(event.key.scancode);
                if(MLG_VERIFY(scancode < m_KeyStates.size()))
                {
                    ++m_KeyStates[scancode].ReleaseCount;
                    m_KeyStates[scancode].DownState = false;
                }
            }
            else
            {
                // Enqueue an action only if the key was not already down.
                const unsigned scancode = static_cast<unsigned>(event.key.scancode);
                if(MLG_VERIFY(scancode < m_KeyStates.size()))
                {
                    // Ignore key repeat events.
                    if(!m_KeyStates[scancode].DownState)
                    {
                        ++m_KeyStates[scancode].PressCount;
                        m_KeyStates[scancode].DownState = true;
                    }
                }
            }
        }
        break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if(event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                const unsigned button = static_cast<unsigned>(event.button.button);
                if(MLG_VERIFY(button < m_MouseButtonStates.size()))
                {
                    ++m_MouseButtonStates[button].ReleaseCount;
                    m_MouseButtonStates[button].DownState = false;
                }
            }
            else
            {
                // Enqueue an action event only if the button was not already down.
                const unsigned button = static_cast<unsigned>(event.button.button);
                if(MLG_VERIFY(button < m_MouseButtonStates.size()))
                {
                    // Ignore button repeat events.
                    if(!m_MouseButtonStates[button].DownState)
                    {
                        ++m_MouseButtonStates[button].PressCount;
                        m_MouseButtonStates[button].DownState = true;
                    }
                }
            }
        }
        break;

        case SDL_EVENT_MOUSE_WHEEL:
            m_MouseWheelDelta.x += event.wheel.x;
            m_MouseWheelDelta.y += event.wheel.y;
            break;

        case SDL_EVENT_MOUSE_MOTION:
            m_MouseDelta.x += event.motion.xrel;
            m_MouseDelta.y += event.motion.yrel;
            break;

        default:
            break;
    }
}

void
InputMapper::EndFrame()
{
    MLG_ASSERT(m_InFrame, "EndFrame() called without a matching BeginFrame()");

    if(m_MouseDelta.x != 0.0f)
    {
        TriggerAction(InputAxis::MouseMoveX, m_MouseDelta.x);
    }

    if(m_MouseDelta.y != 0.0f)
    {
        TriggerAction(InputAxis::MouseMoveY, m_MouseDelta.y);
    }

    if(m_MouseWheelDelta.x != 0.0f)
    {
        TriggerAction(InputAxis::MouseWheelX, m_MouseWheelDelta.x);
    }

    if(m_MouseWheelDelta.y != 0.0f)
    {
        TriggerAction(InputAxis::MouseWheelY, m_MouseWheelDelta.y);
    }

    m_MouseDelta = Vec2f(0);
    m_MouseWheelDelta = Vec2f(0);

    // Synthesize button events.

    for(const ButtonActionMapping& mapping : m_ButtonActionMappings)
    {
        const ButtonState* buttonState = nullptr;

        switch(mapping.Button.GetDevice())
        {
            case InputButtonDevice::Mouse:
                if(MLG_VERIFY(mapping.Button.GetId() < m_MouseButtonStates.size()))
                {
                    buttonState = &m_MouseButtonStates[mapping.Button.GetId()];
                }
                break;

            case InputButtonDevice::Keyboard:
                if(MLG_VERIFY(mapping.Button.GetId() < m_KeyStates.size()))
                {
                    buttonState = &m_KeyStates[mapping.Button.GetId()];
                }
                break;
        }

        if(MLG_VERIFY(buttonState))
        {
            if((mapping.Button.TriggersOnPress() && buttonState->IsPressed())
                || (mapping.Button.TriggersOnRelease() && buttonState->IsReleased())
                || (mapping.Button.TriggersWhileDown() && buttonState->IsDown()))
            {
                TriggerAction(mapping);
            }
        }
    }

    for(auto& buttonState : m_KeyStates)
    {
        buttonState.PressCount = 0;
        buttonState.ReleaseCount = 0;
    }

    for(auto& buttonState : m_MouseButtonStates)
    {
        buttonState.PressCount = 0;
        buttonState.ReleaseCount = 0;
    }

    m_InFrame = false;
}

bool
InputMapper::Action(const ActionIdentifier& actionId) const
{
    MLG_ASSERT(!m_InFrame, "Action() called during BeginFrame()/EndFrame()");

    for(const ActionState& actionState : m_ActionStates)
    {
        if(actionState.ActionId == actionId)
        {
            return actionState.Triggered;
        }
    }

    return false;
}

bool
InputMapper::Action(const ActionIdentifier& actionId, float& value) const
{
    MLG_ASSERT(!m_InFrame, "Action() called during BeginFrame()/EndFrame()");

    for(const ActionState& actionState : m_ActionStates)
    {
        if(actionState.ActionId == actionId && actionState.Triggered)
        {
            value = actionState.Value;
            return true;
        }
    }

    value = 0;
    return false;
}

void
InputMapper::TriggerAction(const ButtonActionMapping& mapping)
{
    mapping.m_ActionState->Triggered = true;
    const float actionValue = mapping.Scale;

    // The event that generates the highest absolute value takes precedence.
    if(std::abs(actionValue) > std::abs(mapping.m_ActionState->Value))
    {
        mapping.m_ActionState->Value = actionValue;
    }
}

void
InputMapper::TriggerAction(const InputAxis& inputAxis, const float value)
{
    for(const AxisActionMapping& mapping : m_AxisActionMappings)
    {
        if(mapping.Axis == inputAxis)
        {
            mapping.m_ActionState->Triggered = true;
            const float actionValue = mapping.Scale * value;

            // The event that generates the highest absolute value takes precedence.
            if(std::abs(actionValue) > std::abs(mapping.m_ActionState->Value))
            {
                mapping.m_ActionState->Value = actionValue;
            }
        }
    }
}

InputMapper::ActionState*
InputMapper::GetActionState(const ActionIdentifier& actionId)
{
    const auto it = std::ranges::lower_bound(m_ActionStates, actionId, {}, &ActionState::ActionId);
    if(it != m_ActionStates.end() && it->ActionId == actionId)
    {
        return &(*it);
    }

    return nullptr;
}