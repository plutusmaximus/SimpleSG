#include "InputMapper.h"

#include <algorithm>
#include <cmath>
#include <SDL3/SDL_scancode.h>
#include <span>

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

static_assert(InputMapper::kMaxKeyButtons >= SDL_SCANCODE_COUNT,
    "kMaxKeyButtons must be at least SDL_SCANCODE_COUNT");

InputMapper::InputMapper(const std::span<const ActionMapping> mappings)
{
    m_InputTriggerMappings.reserve(mappings.size());
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
        if(mapping.Trigger.GetType() == InputTrigger::Type::Button)
        {
            const InputButton& button = mapping.Trigger.GetButton();

            MLG_ABORTIF(!ValidateInputButton(button),
                "Invalid InputButton mapping: device={}, id={}",
                static_cast<int>(button.GetDevice()),
                button.GetId());
        }

        const size_t actionStateIndex = GetActionStateIndex(mapping.ActionId);
        m_InputTriggerMappings.emplace_back(mapping.Trigger, mapping.Scale, actionStateIndex);
    }
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
        keyState.HeldState = false;
    }

    for(auto& mouseButtonState : m_MouseButtonStates)
    {
        mouseButtonState.PressCount = 0;
        mouseButtonState.ReleaseCount = 0;
        mouseButtonState.HeldState = false;
    }

    m_MouseDelta = Vec3f{ 0, 0, 0 };
    m_MouseWheelDelta = Vec3f{ 0, 0, 0 };
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
InputMapper::OnButtonPressed(const InputButtonDevice device, const unsigned buttonId)
{
    switch(device)
    {
        case InputButtonDevice::Keyboard:
            if(MLG_VERIFY(buttonId < m_KeyStates.size()))
            {
                // Ignore key repeat events.
                if(!m_KeyStates[buttonId].HeldState)
                {
                    ++m_KeyStates[buttonId].PressCount;
                    m_KeyStates[buttonId].HeldState = true;
                }
            }
            break;
        case InputButtonDevice::Mouse:
            if(MLG_VERIFY(buttonId < m_MouseButtonStates.size()))
            {
                // Ignore key repeat events.
                if(!m_MouseButtonStates[buttonId].HeldState)
                {
                    ++m_MouseButtonStates[buttonId].PressCount;
                    m_MouseButtonStates[buttonId].HeldState = true;
                }
            }
            break;
        default:
            MLG_ASSERT(false, "Unknown input device");
            break;
    }
}

void
InputMapper::OnButtonReleased(const InputButtonDevice device, const unsigned buttonId)
{
    switch(device)
    {
        case InputButtonDevice::Keyboard:
            if(MLG_VERIFY(buttonId < m_KeyStates.size()))
            {
                ++m_KeyStates[buttonId].ReleaseCount;
                m_KeyStates[buttonId].HeldState = false;
            }
            break;
        case InputButtonDevice::Mouse:
            if(MLG_VERIFY(buttonId < m_MouseButtonStates.size()))
            {
                ++m_MouseButtonStates[buttonId].ReleaseCount;
                m_MouseButtonStates[buttonId].HeldState = false;
            }
            break;
        default:
            MLG_ASSERT(false, "Unknown input device");
            break;
    }
}

void
InputMapper::OnAxis(
    const InputAxisDevice device, const InputAxisIdentifier axisId, const float value)
{
    switch(device)
    {
        case InputAxisDevice::Mouse:
            switch(axisId)
            {
                case InputAxisIdentifier::X:
                    m_MouseDelta.x += value;
                    break;
                case InputAxisIdentifier::Y:
                    m_MouseDelta.y += value;
                    break;
                case InputAxisIdentifier::Z:
                    m_MouseDelta.z += value;
                    break;
            }
            break;
        case InputAxisDevice::MouseWheel:
            switch(axisId)
            {
                case InputAxisIdentifier::X:
                    m_MouseWheelDelta.x += value;
                    break;
                case InputAxisIdentifier::Y:
                    m_MouseWheelDelta.y += value;
                    break;
                case InputAxisIdentifier::Z:
                    m_MouseWheelDelta.z += value;
                    break;
            }
            break;
    }
}

void
InputMapper::EndFrame()
{
    MLG_ASSERT(m_InFrame, "EndFrame() called without a matching BeginFrame()");

    for(const InputTriggerMapping& mapping : m_InputTriggerMappings)
    {
        const std::optional<float> value = EvaluateTrigger(mapping.Trigger);

        if(value)
        {
            ActionState& actionState = m_ActionStates[mapping.ActionStateIndex];

            actionState.Triggered = true;
            const float actionValue = mapping.Scale * value.value();

            // The event that generates the highest absolute value takes precedence.
            if(std::abs(actionValue) > std::abs(actionState.Value))
            {
                actionState.Value = actionValue;
            }
        }
    }

    m_MouseDelta = Vec3f(0);
    m_MouseWheelDelta = Vec3f(0);

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
InputMapper::IsActionTriggered(const ActionIdentifier& actionId) const
{
    float value = 0.0f;
    return IsActionTriggered(actionId, value);
}

bool
InputMapper::IsActionTriggered(const ActionIdentifier& actionId, float& value) const
{
    MLG_ASSERT(!m_InFrame, "IsActionTriggered() called during BeginFrame()/EndFrame()");

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

// private:

size_t
InputMapper::GetActionStateIndex(const ActionIdentifier& actionId) const
{
    const auto it = std::ranges::lower_bound(m_ActionStates, actionId, {}, &ActionState::ActionId);
    MLG_ABORTIF(it == m_ActionStates.end() || it->ActionId != actionId, "ActionState not found");

    return static_cast<size_t>(std::distance(m_ActionStates.begin(), it));
}

std::optional<float>
InputMapper::EvaluateTrigger(const InputTrigger& trigger) const
{
    switch(trigger.GetType())
    {
        case InputTrigger::Type::Button:
            return EvaluateButton(trigger.GetButton());
        case InputTrigger::Type::Axis:
            return EvaluateAxis(trigger.GetAxis());
        default:
            return std::nullopt;
    }

    return std::nullopt;
}

std::optional<float>
InputMapper::EvaluateButton(const InputButton& button) const
{
    const ButtonState* state = nullptr;
    switch(button.GetDevice())
    {
        case InputButtonDevice::Keyboard:
            if(button.GetId() < m_KeyStates.size())
            {
                state = &m_KeyStates[button.GetId()];
            }
            break;
        case InputButtonDevice::Mouse:
            if(button.GetId() < m_MouseButtonStates.size())
            {
                state = &m_MouseButtonStates[button.GetId()];
            }
            break;
        default:
            break;
    }

    if(!state)
    {
        return std::nullopt;
    }

    const bool triggered = (button.TriggersOnPress() && state->IsPressed())
        || (button.TriggersOnRelease() && state->IsReleased())
        || (button.TriggersWhileHeld() && state->IsHeld());

    return triggered ? std::optional<float>{ 1.0f } : std::nullopt;
}

std::optional<float>
InputMapper::EvaluateAxis(const InputAxis& axis) const
{
    float value = 0.0;

    switch(axis.GetDevice())
    {
        case InputAxisDevice::Mouse:
            switch(axis.GetAxisId())
            {
                case InputAxisIdentifier::X:
                    value = m_MouseDelta.x;
                    break;
                case InputAxisIdentifier::Y:
                    value = m_MouseDelta.y;
                    break;
                case InputAxisIdentifier::Z:
                    value = m_MouseDelta.z;
                    break;
                default:
                    break;
            }
            break;
        case InputAxisDevice::MouseWheel:
            switch(axis.GetAxisId())
            {
                case InputAxisIdentifier::X:
                    value = m_MouseWheelDelta.x;
                    break;
                case InputAxisIdentifier::Y:
                    value = m_MouseWheelDelta.y;
                    break;
                case InputAxisIdentifier::Z:
                    value = m_MouseWheelDelta.z;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    return value != 0.0f ? std::optional<float>{ value } : std::nullopt;
}