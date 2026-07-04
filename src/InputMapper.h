#pragma once

#include "VecMath.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

enum class InputButtonType
{
    Key,
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

enum class InputAxisType
{
    Mouse,
    MouseWheel,
    GamepadLeft,
    GamepadRight
};

enum class InputAxisDirection
{
    LeftRight,
    UpDown
};

/// @brief A unique identifier for an input button.
class ButtonIdentifier
{
public:

    ButtonIdentifier() = delete;

    /// @brief Construct a ButtonIdentifier with the specified type and id.
    /// @param type The type of the input button.
    /// @param id The identifier for the button within its type.  E.g. for a keyboard key, this
    /// would be the SDL scancode.  For a mouse button, this would be the SDL mouse button code. For
    /// a gamepad button, this would be the SDL gamepad button code.
    constexpr ButtonIdentifier(const InputButtonType type, const unsigned id)
        : m_Type(type),
          m_Id(id)
    {
    }

    constexpr InputButtonType GetType() const { return m_Type; }
    constexpr unsigned GetId() const { return m_Id; }

private:

    InputButtonType m_Type;
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

    constexpr InputAxis(const InputAxisType type, const InputAxisDirection direction)
        : m_Type(type),
          m_Direction(direction)
    {
    }

    constexpr InputAxisType GetType() const { return m_Type; }
    constexpr InputAxisDirection GetDirection() const { return m_Direction; }

private:
    InputAxisType m_Type;
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

/// @brief Input events are triggered by actions that are mapped to input buttons or axes.
///       Input events are dispatched to handlers that can respond to the input.
struct InputEvent
{
    ActionIdentifier ActionId;
    std::chrono::duration<int64_t, std::micro> Timestamp;
    // For axis events, this is the value of the axis movement
    // For button events, this is always 1.0f
    float Value{0.0f};
};

/// @brief InputEventHandler contains a callable object that will be invoked when an input event occurs.
class InputEventHandler
{
public:
    InputEventHandler() = delete;
    ~InputEventHandler() { m_Destroy(GetStorage()); }
    InputEventHandler(const InputEventHandler&) = delete;
    InputEventHandler& operator=(const InputEventHandler&) = delete;
    InputEventHandler(InputEventHandler&& other) = delete;
    InputEventHandler& operator=(InputEventHandler&& other) = delete;

    template<class F>
    InputEventHandler(F&& f) // NOLINT(google-explicit-constructor)
        : m_Invoke(&InputEventHandler::Invoke<std::remove_cvref_t<F>>),
          m_Destroy(&InputEventHandler::Destroy<std::remove_cvref_t<F>>)
    {
        static_assert(std::is_invocable_r_v<void, std::remove_cvref_t<F>&, const InputEvent&>);
        static_assert(sizeof(F) <= kCapacity, "Callable too large for InputEventHandler");
        static_assert(alignof(F) <= kAlign, "Callable alignment too large for InputEventHandler");

        F* storage = static_cast<F*>(GetStorage());

        std::construct_at(storage, std::forward<F>(f));
    }

private:

    friend class InputMapper;

    static constexpr std::size_t kAlign = alignof(std::max_align_t);
    static constexpr std::size_t kCapacity = 64;
    alignas(kAlign) std::byte m_Bytes[kCapacity]{};

    void Dispatch(const InputEvent& e) const
    {
        m_Invoke(GetStorage(), e);
    }

    template<typename F>
    static void Invoke(const void* p, const InputEvent& e)
    {
        (*static_cast<const F*>(p))(e);
    }

    template<typename F>
    static void Destroy(void* p)
    {
        std::destroy_at(static_cast<F*>(p));
    }

    void (*m_Invoke)(const void*, const InputEvent&) = nullptr;
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

struct ActionMapping
{
    ActionIdentifier ActionId;
    InputEventHandler Handler;
};

/// @brief Represents a mapping between an input button or axis and an input action.
/// When an input event occurs for the specified button or axis, the handler will be called
/// with an input event that contains the associated action identifier and a timestamp.
struct InputMapping // NOLINT(clang-analyzer-optin.performance.Padding)
{
    std::variant<InputButton, InputAxis> Input;
    ActionIdentifier ActionId;
    InputEventHandler Handler;
    float Scale = 1;
};

class InputMapper
{
public:

    InputMapper() = delete;

    explicit InputMapper(const std::span<InputMapping> mappings);

    void ProcessEvent(const union SDL_Event& event);

    void DispatchEvents();

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

        QueuedEvent(const InputEvent& event, const InputEventHandler& handler)
            : Event(event),
              Handler(&handler)
        {
        }

        InputEvent Event;
        const InputEventHandler* Handler{nullptr};
    };

    void HandleButtonEvent(const ButtonIdentifier& buttonId,
        std::chrono::duration<int64_t, std::micro> timestamp,
        const bool pressed);

    void SynthesizeEvents();

    using MappingRange = std::ranges::subrange<std::vector<const InputMapping*>::iterator>;

    MappingRange GetMappings(const InputButton& button) const;

    MappingRange GetMappings(const InputAxis& axis) const;

    ButtonState* GetButtonState(const ButtonIdentifier& buttonId);

    void EnqueueEvent(const InputEvent& event, const InputEventHandler& handler);

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
        : InputButton(ButtonIdentifier(InputButtonType::Key, KEY_CODE), InputButtonState::Pressed)
    {
    }
};

template<unsigned KEY_CODE>
class KeyReleased : public InputButton
{
public:
    KeyReleased()
        : InputButton(ButtonIdentifier(InputButtonType::Key, KEY_CODE), InputButtonState::Released)
    {
    }
};

template<unsigned KEY_CODE>
class KeyDown : public InputButton
{
public:
    KeyDown()
        : InputButton(ButtonIdentifier(InputButtonType::Key, KEY_CODE), InputButtonState::Down)
    {
    }
};

template<unsigned BUTTON_CODE>
class MousePressed : public InputButton
{
public:
    MousePressed()
        : InputButton(ButtonIdentifier(InputButtonType::Mouse, BUTTON_CODE),
              InputButtonState::Pressed)
    {
    }
};

template<unsigned BUTTON_CODE>
class MouseReleased : public InputButton
{
public:
    MouseReleased()
        : InputButton(ButtonIdentifier(InputButtonType::Mouse, BUTTON_CODE),
              InputButtonState::Released)
    {
    }
};

template<unsigned BUTTON_CODE>
class MouseDown : public InputButton
{
public:
    MouseDown()
        : InputButton(ButtonIdentifier(InputButtonType::Mouse, BUTTON_CODE), InputButtonState::Down)
    {
    }
};

class MouseMoveLeftRight : public InputAxis
{
public:
    MouseMoveLeftRight()
        : InputAxis(InputAxisType::Mouse, InputAxisDirection::LeftRight)
    {
    }
};
class MouseMoveUpDown : public InputAxis
{
public:
    MouseMoveUpDown()
        : InputAxis(InputAxisType::Mouse, InputAxisDirection::UpDown)
    {
    }
};

class MouseWheelLeftRight : public InputAxis
{
public:
    MouseWheelLeftRight()
        : InputAxis(InputAxisType::MouseWheel, InputAxisDirection::LeftRight)
    {
    }
};

class MouseWheelUpDown : public InputAxis
{
public:
    MouseWheelUpDown()
        : InputAxis(InputAxisType::MouseWheel, InputAxisDirection::UpDown)
    {
    }
};