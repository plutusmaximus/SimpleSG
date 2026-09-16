#include "InputMapper.h"

#include <algorithm>
#include <cmath>
#include <SDL3/SDL_events.h>
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
    // Count mappings so action arrays can be allocated.
    size_t buttonMappingCount = 0;
    size_t axisMappingCount = 0;
    for(const ActionMapping& mapping : mappings)
    {
        switch(mapping.Trigger.GetType())
        {
            case InputTrigger::Type::Button:
                ++buttonMappingCount;
                break;
            case InputTrigger::Type::Axis:
                ++axisMappingCount;
                break;
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
        switch(mapping.Trigger.GetType())
        {
            case InputTrigger::Type::Button:
            {
                const InputButton& button = mapping.Trigger.GetButton();

                MLG_ABORTIF(!ValidateInputButton(button),
                    "Invalid InputButton mapping: device={}, id={}",
                    static_cast<int>(button.GetDevice()),
                    button.GetId());

                ButtonActionMapping& bam =
                    m_ButtonActionMappings.emplace_back(button, mapping.Scale);
                bam.m_ActionState = GetActionState(mapping.ActionId);
            }
            break;
            case InputTrigger::Type::Axis:
            {
                const InputAxis& axis = mapping.Trigger.GetAxis();

                AxisActionMapping& aam = m_AxisActionMappings.emplace_back(axis, mapping.Scale);
                aam.m_ActionState = GetActionState(mapping.ActionId);
            }
            break;
        }
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

    int numKeys = 0;
    const bool* keyboardState = SDL_GetKeyboardState(&numKeys);

    const size_t keyCount = static_cast<size_t>(numKeys);
    MLG_ASSERT(keyCount == m_KeyStates.size(),
        "SDL_GetKeyboardState() returned unexpected number of keys");

    const std::span<const bool> keyboardStateSpan(keyboardState, keyCount);

    for(size_t i = 0; i < keyCount && i < m_KeyStates.size(); ++i)
    {
        m_KeyStates[i].HeldState = keyboardStateSpan[i];
    }

    const SDL_MouseButtonFlags mouseButtonBits = SDL_GetMouseState(nullptr, nullptr);

    for(size_t i = 1; i < m_MouseButtonStates.size(); ++i)
    {
        const unsigned buttonMask = SDL_BUTTON_MASK(i);

        m_MouseButtonStates[i].HeldState = (mouseButtonBits & buttonMask) != 0;
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

    if(m_MouseDelta.x != 0.0f)
    {
        TriggerAction(InputAxis::MouseMoveX(), m_MouseDelta.x);
    }

    if(m_MouseDelta.y != 0.0f)
    {
        TriggerAction(InputAxis::MouseMoveY(), m_MouseDelta.y);
    }

    if(m_MouseDelta.z != 0.0f)
    {
        TriggerAction(InputAxis::MouseMoveZ(), m_MouseDelta.z);
    }

    if(m_MouseWheelDelta.x != 0.0f)
    {
        TriggerAction(InputAxis::MouseWheelX(), m_MouseWheelDelta.x);
    }

    if(m_MouseWheelDelta.y != 0.0f)
    {
        TriggerAction(InputAxis::MouseWheelY(), m_MouseWheelDelta.y);
    }

    m_MouseDelta = Vec3f(0);
    m_MouseWheelDelta = Vec3f(0);

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
                || (mapping.Button.TriggersWhileHeld() && buttonState->IsHeld()))
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
InputMapper::IsActionTriggered(const ActionIdentifier& actionId) const
{
    MLG_ASSERT(!m_InFrame, "IsActionTriggered() called during BeginFrame()/EndFrame()");

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