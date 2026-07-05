#include "InputMapper.h"

#include "AssertHelper.h"

#include <algorithm>
#include <span>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

namespace
{

constexpr bool ButtonIdLt(const ButtonIdentifier& a, const ButtonIdentifier& b)
{
    if(a.GetDevice() != b.GetDevice())
    {
        return a.GetDevice() < b.GetDevice();
    }

    return a.GetId() < b.GetId();
}

constexpr bool operator==(const ButtonIdentifier& a, const ButtonIdentifier& b)
{
    return a.GetDevice() == b.GetDevice() && a.GetId() == b.GetId();
}

constexpr bool ButtonIdEq(const ButtonIdentifier& a, const ButtonIdentifier& b)
{
    return a == b;
}

constexpr bool InputButtonLt(const InputButton& a, const InputButton& b)
{
    if(a.GetButtonId() != b.GetButtonId())
    {
        return ButtonIdLt(a.GetButtonId(), b.GetButtonId());
    }

    return a.GetState() < b.GetState();
}

constexpr bool InputAxisLt(const InputAxis& a, const InputAxis& b)
{
    if(a.GetDevice() != b.GetDevice())
    {
        return a.GetDevice() < b.GetDevice();
    }

    return a.GetDirection() < b.GetDirection();
}

constexpr bool InputMappingLt(const InputMapping* a, const InputMapping* b)
{
    if(std::holds_alternative<InputButton>(a->Input) && std::holds_alternative<InputButton>(b->Input))
    {
        const InputButton& buttonA = std::get<InputButton>(a->Input);
        const InputButton& buttonB = std::get<InputButton>(b->Input);

        return InputButtonLt(buttonA, buttonB);
    }

    if(std::holds_alternative<InputAxis>(a->Input) && std::holds_alternative<InputAxis>(b->Input))
    {
        const InputAxis& axisA = std::get<InputAxis>(a->Input);
        const InputAxis& axisB = std::get<InputAxis>(b->Input);

        return InputAxisLt(axisA, axisB);
    }

    return a->Input.index() < b->Input.index();
}
} // namespace

InputMapper::InputMapper(const std::span<InputMapping> mappings)
{
    size_t buttonCount = 0;
    for(const InputMapping& mapping : mappings)
    {
        if(std::holds_alternative<InputButton>(mapping.Input))
        {
            ++buttonCount;
        }
    }

    m_Mappings.reserve(mappings.size());
    m_ButtonStates.reserve(buttonCount);
    m_EventQueue.reserve(kMaxEventQueueSize);

    for(const InputMapping& mapping : mappings)
    {
        m_Mappings.push_back(&mapping);

        if(std::holds_alternative<InputButton>(mapping.Input))
        {
            const InputButton& button = std::get<InputButton>(mapping.Input);
            m_ButtonStates.emplace_back(button.GetButtonId());
        }
    }

    std::ranges::sort(m_Mappings, InputMappingLt);
    std::ranges::sort(m_ButtonStates, ButtonIdLt, &ButtonState::ButtonId);

    auto buttonPredicate = [](const InputMapping* mapping)
    {
        return std::holds_alternative<InputButton>(mapping->Input);
    };

    auto axisPredicate = [](const InputMapping* mapping)
    {
        return std::holds_alternative<InputAxis>(mapping->Input);
    };

    auto firstButton = std::ranges::find_if(m_Mappings, buttonPredicate);
    auto lastButton = std::ranges::find_if_not(firstButton, m_Mappings.end(), buttonPredicate);

    auto firstAxis = std::ranges::find_if(m_Mappings, axisPredicate);
    auto lastAxis = std::ranges::find_if_not(firstAxis, m_Mappings.end(), axisPredicate);
    
    m_ButtonMappings = std::span<const InputMapping*>(firstButton, lastButton);
    m_AxisMappings = std::span<const InputMapping*>(firstAxis, lastAxis);

    // Duplicates may exist in tracked buttons due to mapping different states
    // of the same button to different actions.
    auto removed = std::ranges::unique(m_ButtonStates, ButtonIdEq, &ButtonState::ButtonId);
    m_ButtonStates.erase(removed.begin(), removed.end());
}

void
InputMapper::ProcessEvent(const SDL_Event& event)
{
    const std::chrono::duration<int64_t, std::nano> timestampNano(event.button.timestamp);
    const std::chrono::duration<int64_t, std::micro> timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(timestampNano);

    switch(event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const ButtonIdentifier buttonId(InputButtonDevice::Keyboard, event.key.scancode);
            const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
            HandleButtonEvent(buttonId, timestamp, pressed);
        }
        break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const ButtonIdentifier buttonId(InputButtonDevice::Mouse, event.button.button);
            const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            HandleButtonEvent(buttonId, timestamp, pressed);
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
InputMapper::DispatchEvents()
{
    // Dispatch naturally occurring events.
    for(const QueuedEvent& qe : m_EventQueue)
    {
        (*qe.Handler)(qe.Event);
    }

    m_EventQueue.clear();

    // Synthesize and dispatch events for buttons held down and mouse movement/wheel.
    SynthesizeEvents();

    for(const QueuedEvent& qe : m_EventQueue)
    {
        (*qe.Handler)(qe.Event);
    }

    m_EventQueue.clear();
}

void
InputMapper::ClearEventQueue()
{
    m_EventQueue.clear();
}

// private:

void
InputMapper::HandleButtonEvent(const ButtonIdentifier& buttonId,
    std::chrono::duration<int64_t, std::micro> timestamp,
    const bool pressed)
{
    ButtonState* buttonState = GetButtonState(buttonId);
    if(!buttonState)
    {
        return;
    }

    MappingRange mappings;

    if(pressed)
    {
        if(!buttonState->Pressed)
        {
            buttonState->Pressed = true;
            const InputButton button(buttonId, InputButtonState::Pressed);
            mappings = GetMappings(button);
        }
    }
    else
    {
        buttonState->Pressed = false;
        buttonState->WasPressed = false;
        const InputButton button(buttonId, InputButtonState::Released);
        mappings = GetMappings(button);
    }

    for(const InputMapping* mapping : mappings)
    {
        const ActionEvent actionEvent //
            {
                .ActionId = mapping->ActionId,
                .Timestamp = timestamp,
                .Value = mapping->Scale,
            };

        EnqueueEvent(actionEvent, mapping->Handler);
    }

    buttonState->Pressed = pressed;
}

void
InputMapper::SynthesizeEvents()
{
    const std::chrono::duration<int64_t, std::nano> nanoTimestamp(SDL_GetTicksNS());
    const std::chrono::duration<int64_t, std::micro> timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(nanoTimestamp);

    if(m_MouseDelta.x != 0.0f)
    {
        const InputAxis axis(InputAxisDevice::Mouse, InputAxisDirection::X);
        const MappingRange mappings = GetMappings(axis);
        for(const InputMapping* mapping : mappings)
        {
            const ActionEvent actionEvent //
                {
                    .ActionId = mapping->ActionId,
                    .Timestamp = timestamp,
                    .Value = m_MouseDelta.x * mapping->Scale,
                };

            EnqueueEvent(actionEvent, mapping->Handler);
        }
    }

    if(m_MouseDelta.y != 0.0f)
    {
        const InputAxis axis(InputAxisDevice::Mouse, InputAxisDirection::Y);
        const MappingRange mappings = GetMappings(axis);
        for(const InputMapping* mapping : mappings)
        {
            const ActionEvent actionEvent //
                {
                    .ActionId = mapping->ActionId,
                    .Timestamp = timestamp,
                    .Value = m_MouseDelta.y * mapping->Scale,
                };

            EnqueueEvent(actionEvent, mapping->Handler);
        }
    }

    if(m_MouseWheelDelta.x != 0.0f)
    {
        const InputAxis axis(InputAxisDevice::MouseWheel, InputAxisDirection::X);
        const MappingRange mappings = GetMappings(axis);
        for(const InputMapping* mapping : mappings)
        {
            const ActionEvent actionEvent //
                {
                    .ActionId = mapping->ActionId,
                    .Timestamp = timestamp,
                    .Value = m_MouseWheelDelta.x * mapping->Scale,
                };

            EnqueueEvent(actionEvent, mapping->Handler);
        }
    }

    if(m_MouseWheelDelta.y != 0.0f)
    {
        const InputAxis axis(InputAxisDevice::MouseWheel, InputAxisDirection::Y);
        const MappingRange mappings = GetMappings(axis);
        for(const InputMapping* mapping : mappings)
        {
            const ActionEvent actionEvent //
                {
                    .ActionId = mapping->ActionId,
                    .Timestamp = timestamp,
                    .Value = m_MouseWheelDelta.y * mapping->Scale,
                };

            EnqueueEvent(actionEvent, mapping->Handler);
        }
    }

    // Synthesize InputButtonState::Down
    for(ButtonState& buttonState : m_ButtonStates)
    {
        if(buttonState.Pressed && !buttonState.WasPressed)
        {
            buttonState.WasPressed = true;
        }
        else if(buttonState.WasPressed)
        {
            MLG_ASSERT(buttonState.Pressed,
                "Button must be pressed to synthesize InputButtonState::Down");

            const InputButton button(buttonState.ButtonId, InputButtonState::Down);
            const MappingRange mappings = GetMappings(button);
            for(const InputMapping* mapping : mappings)
            {
                const ActionEvent actionEvent //
                    {
                        .ActionId = mapping->ActionId,
                        .Timestamp = timestamp,
                        .Value = mapping->Scale,
                    };

                EnqueueEvent(actionEvent, mapping->Handler);
            }
        }
    }

    m_MouseDelta = Vec2f(0);
    m_MouseWheelDelta = Vec2f(0);
}

InputMapper::MappingRange
InputMapper::GetMappings(const InputButton& button) const
{
    auto proj = [](const InputMapping* mapping) -> const InputButton&
    {
        return std::get<InputButton>(mapping->Input);
    };

    std::ranges::subrange<std::vector<const InputMapping*>::iterator> mappings;
    mappings = std::ranges::equal_range(m_ButtonMappings, button, InputButtonLt, proj);

    return mappings;
}

InputMapper::MappingRange
InputMapper::GetMappings(const InputAxis& axis) const
{
    auto proj = [](const InputMapping* mapping) -> const InputAxis&
    {
        return std::get<InputAxis>(mapping->Input);
    };

    std::ranges::subrange<std::vector<const InputMapping*>::iterator> mappings;
    mappings = std::ranges::equal_range(m_AxisMappings, axis, InputAxisLt, proj);

    return mappings;
}

InputMapper::ButtonState*
InputMapper::GetButtonState(const ButtonIdentifier& buttonId)
{
    auto it =
        std::ranges::lower_bound(m_ButtonStates, buttonId, ButtonIdLt, &ButtonState::ButtonId);

    return (it == m_ButtonStates.end() || it->ButtonId != buttonId) ? nullptr : &(*it);
}

void
InputMapper::EnqueueEvent(const ActionEvent& event, const ActionHandler& handler)
{
    if(!MLG_VERIFY(m_EventQueue.size() < kMaxEventQueueSize, "Event queue is full"))
    {
        return;
    }

    m_EventQueue.emplace_back(event, handler);
}