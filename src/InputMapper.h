#pragma once

#include "VecMath.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

/*
Event   = observed fact
Action  = semantic intent
Command = executable mutation
Message = transport/envelope

Event       -> Action
Action      -> Command
Command     -> Event(s)
*/

enum class InputButtonDevice
{
    Keyboard,
    Mouse,
    Gamepad
};

enum class InputButtonState
{
    // Button pressed this frame.
    Pressed,
    // Button released this frame.
    Released,
    // Button is being held down.
    Down
};

enum class InputAxisDevice
{
    Mouse,
    MouseWheel,
    GamepadLeft,
    GamepadRight
};

enum class InputAxisDirection
{
    X,
    Y,
    Z
};

/// @brief A unique identifier for an input button.
class ButtonIdentifier
{
public:

    ButtonIdentifier() = delete;

    /// @brief Construct a ButtonIdentifier with the specified device and id.
    /// @param device The device of the input button.
    /// @param id The identifier for the button within its device.  E.g. for a keyboard key, this
    /// would be the SDL scancode.  For a mouse button, this would be the SDL mouse button code. For
    /// a gamepad button, this would be the SDL gamepad button code.
    constexpr ButtonIdentifier(const InputButtonDevice device, const unsigned id)
        : m_Device(device),
          m_Id(id)
    {
    }

    constexpr InputButtonDevice GetDevice() const { return m_Device; }
    constexpr unsigned GetId() const { return m_Id; }

private:

    InputButtonDevice m_Device;
    unsigned m_Id;
};

/// @brief Represents a specific input button and its state (pressed, released, down).
///       This is used to map input events to actions in the InputMapper.
class InputButton
{
public:
    InputButton() = delete;

    constexpr InputButton(const ButtonIdentifier& buttonId, const InputButtonState state)
        : m_ButtonId(buttonId),
          m_State(state)
    {
    }

    constexpr const ButtonIdentifier& GetButtonId() const { return m_ButtonId; }
    constexpr InputButtonState GetState() const { return m_State; }

private:

    ButtonIdentifier m_ButtonId;
    InputButtonState m_State;
};

/// @brief Represents a specific input axis and its direction (left-right, up-down).
///       This is used to map input events to actions in the InputMapper.
class InputAxis
{
public:
    InputAxis() = delete;

    constexpr InputAxis(const InputAxisDevice device, const InputAxisDirection direction)
        : m_Device(device),
          m_Direction(direction)
    {
    }

    constexpr InputAxisDevice GetDevice() const { return m_Device; }
    constexpr InputAxisDirection GetDirection() const { return m_Direction; }

private:
    InputAxisDevice m_Device;
    InputAxisDirection m_Direction;
};

/// @brief Represents an input action.  Input actions are mapped to input buttons or axes, and can
/// be used to trigger specific behaviors in the application.
class ActionIdentifier
{
public:
    ActionIdentifier() = delete;

    template<size_t N>
    explicit consteval ActionIdentifier(const char (&name)[N])
        : m_Name(&name[0]),
          m_Hash(HashName(name))
    {
        static_assert(N > 0, "ActionIdentifier name must not be empty");
    }

    friend constexpr auto operator<=>(const ActionIdentifier& a, const ActionIdentifier& b)
    {
        return a.m_Hash <=> b.m_Hash;
    }

    friend constexpr bool operator==(const ActionIdentifier& a, const ActionIdentifier& b)
    {
        return a.m_Hash == b.m_Hash && std::strcmp(a.m_Name, b.m_Name) == 0;
    }

    friend constexpr bool operator!=(const ActionIdentifier& a, const ActionIdentifier& b)
    {
        return !(a == b);
    }

private:
    template<size_t N>
    static consteval uint64_t HashName(const char (&str)[N])
    {
        static constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ull;
        static constexpr uint64_t kFNVPrime = 1099511628211ull;

        uint64_t h = kFNVOffsetBasis;

        // N includes the null terminator, so stop at N - 1.
        for(size_t i = 0; i < N - 1; ++i)
        {
            h ^= static_cast<unsigned char>(str[i]);
            h *= kFNVPrime;
        }

        return h;
    }
    const char* m_Name{ nullptr };
    uint64_t m_Hash{ 0 };
};

struct ActionEvent;

/// @brief ActionHandler contains a callable object that will be invoked when an action is triggered.
class ActionHandler
{
public:
    ActionHandler() = delete;
    ~ActionHandler() { m_Destroy(GetStorage()); }
    ActionHandler(const ActionHandler&) = delete;
    ActionHandler& operator=(const ActionHandler&) = delete;
    ActionHandler(ActionHandler&& other) = delete;
    ActionHandler& operator=(ActionHandler&& other) = delete;

    template<class F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, ActionHandler>)
    ActionHandler(F&& f) // NOLINT(google-explicit-constructor)
        : m_Invoke(&ActionHandler::Invoke<std::remove_cvref_t<F>>),
          m_Destroy(&ActionHandler::Destroy<std::remove_cvref_t<F>>)
    {
        static_assert(std::is_invocable_r_v<void, std::remove_cvref_t<F>&, const ActionEvent&>);
        static_assert(sizeof(F) <= kCapacity, "Callable too large for ActionHandler");
        static_assert(alignof(F) <= kAlign, "Callable alignment too large for ActionHandler");

        F* storage = static_cast<F*>(GetStorage());

        std::construct_at(storage, std::forward<F>(f));
    }

    void operator()(const ActionEvent& e) const
    {
        m_Invoke(GetStorage(), e);
    }

private:

    friend class InputMapper;

    static constexpr std::size_t kAlign = alignof(std::max_align_t);
    static constexpr std::size_t kCapacity = 64;
    alignas(kAlign) std::byte m_Bytes[kCapacity]{};

    template<typename F>
    static void Invoke(const void* p, const ActionEvent& e)
    {
        (*static_cast<const F*>(p))(e);
    }

    template<typename F>
    static void Destroy(void* p)
    {
        std::destroy_at(static_cast<F*>(p));
    }

    void (*m_Invoke)(const void*, const ActionEvent&) = nullptr;
    void (*m_Destroy)(void*) = nullptr;

    void* GetStorage()
    {
        return static_cast<void*>(m_Bytes);
    }

    const void* GetStorage() const
    {
        return static_cast<const void*>(m_Bytes);
    }
};

/// @brief Action events are triggered by input buttons or axes mapped to actions.
///       Action events are dispatched to handlers that can respond to the input.
struct ActionEvent
{
    ActionIdentifier ActionId;
    std::chrono::duration<int64_t, std::micro> Timestamp;
    // For axis events, this is the value of the axis movement
    // For button events, this is always 1.0f
    float Value{0.0f};
};

/// @brief Represents a mapping between an input button or axis and an action.
/// When an input event occurs for the specified button or axis, the handler will be called
/// with an event that contains the associated action identifier and a timestamp.
struct InputMapping // NOLINT(clang-analyzer-optin.performance.Padding)
{
    std::variant<InputButton, InputAxis> Input;
    ActionIdentifier ActionId;
    ActionHandler Handler;
    float Scale = 1;
};

class InputMapper
{
public:

    InputMapper() = delete;

    explicit InputMapper(const std::span<InputMapping> mappings);

    void ProcessEvent(const union SDL_Event& event);

    void DispatchEvents();

    void ClearEventQueue();

private:

    struct ButtonState
    {
        ButtonState() = delete;

        explicit ButtonState(const ButtonIdentifier& buttonId)
            : ButtonId(buttonId)
        {
        }

        ButtonIdentifier ButtonId;
        bool Pressed{false};
        bool WasPressed{false};
    };

    struct QueuedEvent
    {
        QueuedEvent() = delete;

        QueuedEvent(const ActionEvent& event, const ActionHandler& handler)
            : Event(event),
              Handler(&handler)
        {
        }

        ActionEvent Event;
        const ActionHandler* Handler{nullptr};
    };

    void HandleButtonEvent(const ButtonIdentifier& buttonId,
        std::chrono::duration<int64_t, std::micro> timestamp,
        const bool pressed);

    void SynthesizeEvents();

    using MappingRange = std::ranges::subrange<std::vector<const InputMapping*>::iterator>;

    MappingRange GetMappings(const InputButton& button) const;

    MappingRange GetMappings(const InputAxis& axis) const;

    ButtonState* GetButtonState(const ButtonIdentifier& buttonId);

    void EnqueueEvent(const ActionEvent& event, const ActionHandler& handler);

    static constexpr size_t kMaxMappingCount = 256;

    static constexpr size_t kMaxEventQueueSize = 256;

    std::vector<const InputMapping*> m_Mappings;
    std::vector<ButtonState> m_ButtonStates;
    std::vector<QueuedEvent> m_EventQueue;

    std::span<const InputMapping*> m_ButtonMappings;
    std::span<const InputMapping*> m_AxisMappings;

    Vec2f m_MouseDelta{0, 0};
    Vec2f m_MouseWheelDelta{0, 0};
};

template<unsigned KEY_CODE>
class KeyPressed : public InputButton
{
public:
    KeyPressed()
        : InputButton(ButtonIdentifier(InputButtonDevice::Keyboard, KEY_CODE), InputButtonState::Pressed)
    {
    }
};

template<unsigned KEY_CODE>
class KeyReleased : public InputButton
{
public:
    KeyReleased()
        : InputButton(ButtonIdentifier(InputButtonDevice::Keyboard, KEY_CODE), InputButtonState::Released)
    {
    }
};

template<unsigned KEY_CODE>
class KeyDown : public InputButton
{
public:
    KeyDown()
        : InputButton(ButtonIdentifier(InputButtonDevice::Keyboard, KEY_CODE), InputButtonState::Down)
    {
    }
};

template<unsigned BUTTON_CODE>
class MousePressed : public InputButton
{
public:
    MousePressed()
        : InputButton(ButtonIdentifier(InputButtonDevice::Mouse, BUTTON_CODE),
              InputButtonState::Pressed)
    {
    }
};

template<unsigned BUTTON_CODE>
class MouseReleased : public InputButton
{
public:
    MouseReleased()
        : InputButton(ButtonIdentifier(InputButtonDevice::Mouse, BUTTON_CODE),
              InputButtonState::Released)
    {
    }
};

template<unsigned BUTTON_CODE>
class MouseDown : public InputButton
{
public:
    MouseDown()
        : InputButton(ButtonIdentifier(InputButtonDevice::Mouse, BUTTON_CODE), InputButtonState::Down)
    {
    }
};

class MouseMoveX : public InputAxis
{
public:
    MouseMoveX()
        : InputAxis(InputAxisDevice::Mouse, InputAxisDirection::X)
    {
    }
};
class MouseMoveY : public InputAxis
{
public:
    MouseMoveY()
        : InputAxis(InputAxisDevice::Mouse, InputAxisDirection::Y)
    {
    }
};

class MouseWheelX : public InputAxis
{
public:
    MouseWheelX()
        : InputAxis(InputAxisDevice::MouseWheel, InputAxisDirection::X)
    {
    }
};

class MouseWheelY : public InputAxis
{
public:
    MouseWheelY()
        : InputAxis(InputAxisDevice::MouseWheel, InputAxisDirection::Y)
    {
    }
};