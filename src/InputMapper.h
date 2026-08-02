#pragma once

#include "VecMath.h"

#include <cstddef>
#include <span>
#include <variant>
#include <vector>

// Forward decls
union SDL_Event;

/// @brief Represents the device that an input button belongs.
// Devices like gamepads, etc. can be added as they are implemented.
enum class InputButtonDevice
{
    Keyboard,
    Mouse,
};

/// @brief Represents the device that an input axis belongs.
/// Devices like gamepads, etc. can be added as they are implemented.
enum class InputAxisDevice
{
    Mouse,
    MouseWheel
};

/// @brief Represents input button event (pressed, released, down) that
/// triggers an action.
enum class InputButtonTrigger
{
    // Button pressed this frame.
    Pressed,
    // Button released this frame.
    Released,
    // Button is being held down.
    Down
};

/// @brief Represents the identifier of an input axis (X, Y, Z) that can be mapped to an action.
enum class InputAxisIdentifier
{
    X,
    Y,
    Z
};

/// @brief Represents a specific input button and its state (pressed, released, down).
/// Used to map input button events to actions.
class InputButton
{
public:
    InputButton() = delete;

    constexpr InputButton(
        const InputButtonDevice device, const unsigned buttonId, const InputButtonTrigger trigger)
        : m_Device(device),
          m_ButtonId(buttonId),
          m_Trigger(trigger)
    {
    }

    constexpr InputButtonDevice GetDevice() const { return m_Device; }
    constexpr unsigned GetId() const { return m_ButtonId; }
    constexpr InputButtonTrigger GetTrigger() const { return m_Trigger; }

    constexpr bool TriggersOnPress() const { return m_Trigger == InputButtonTrigger::Pressed; }
    constexpr bool TriggersOnRelease() const { return m_Trigger == InputButtonTrigger::Released; }
    constexpr bool TriggersWhileDown() const { return m_Trigger == InputButtonTrigger::Down; }

    friend constexpr bool operator==(const InputButton& a, const InputButton& b) = default;

    /// Helper functions to create InputButton instances for specific button states.

    static constexpr InputButton KeyPressed(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonTrigger::Pressed);
    }

    static constexpr InputButton KeyReleased(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonTrigger::Released);
    }

    static constexpr InputButton KeyDown(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonTrigger::Down);
    }

    static constexpr InputButton MousePressed(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonTrigger::Pressed);
    }

    static constexpr InputButton MouseReleased(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonTrigger::Released);
    }

    static constexpr InputButton MouseDown(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonTrigger::Down);
    }

private:
    InputButtonDevice m_Device;
    unsigned m_ButtonId;
    InputButtonTrigger m_Trigger;
};

/// @brief Represents a specific input axis and its direction (X, Y, Z).
/// Used to map input axis events to actions.
class InputAxis
{
public:
    InputAxis() = delete;

    constexpr InputAxis(const InputAxisDevice device, const InputAxisIdentifier axisId)
        : m_Device(device),
          m_AxisId(axisId)
    {
    }

    constexpr InputAxisDevice GetDevice() const { return m_Device; }
    constexpr InputAxisIdentifier GetAxisId() const { return m_AxisId; }

    friend constexpr bool operator==(const InputAxis& a, const InputAxis& b) = default;

    /// Predefined InputAxis instances.

    static const InputAxis MouseMoveX;
    static const InputAxis MouseMoveY;
    static const InputAxis MouseMoveZ;
    static const InputAxis MouseWheelX;
    static const InputAxis MouseWheelY;

private:
    InputAxisDevice m_Device;
    InputAxisIdentifier m_AxisId;
};

inline const InputAxis InputAxis::MouseMoveX{ InputAxisDevice::Mouse, InputAxisIdentifier::X };
inline const InputAxis InputAxis::MouseMoveY{ InputAxisDevice::Mouse, InputAxisIdentifier::Y };
inline const InputAxis InputAxis::MouseMoveZ{ InputAxisDevice::Mouse, InputAxisIdentifier::Z };
inline const InputAxis InputAxis::MouseWheelX{ InputAxisDevice::MouseWheel,
    InputAxisIdentifier::X };
inline const InputAxis InputAxis::MouseWheelY{ InputAxisDevice::MouseWheel,
    InputAxisIdentifier::Y };

/// @brief Represents a unique identifier for an action that can be mapped to input events.
/// Action identifiers are created at compile time using a string literal.
class ActionIdentifier
{
public:
    ActionIdentifier() = delete;

    template<size_t N>
    explicit consteval ActionIdentifier(const char (&name)[N])
        : m_Hash(HashName(name))
    {
        static_assert(N > 0, "ActionIdentifier name must not be empty");
        static_assert(N <= kMaxNameLength + 1, "ActionIdentifier name is too long");
        for(size_t i = 0; i < N; ++i)
        {
            m_Name[i] = name[i];
        }
    }

    constexpr const char* c_str() const { return &m_Name[0]; }

    friend constexpr auto operator<=>(const ActionIdentifier& a, const ActionIdentifier& b)
    {
        if(a.m_Hash != b.m_Hash)
        {
            return a.m_Hash <=> b.m_Hash;
        }

        return std::strcmp(a.c_str(), b.c_str()) <=> 0;
    }

    friend constexpr bool operator==(const ActionIdentifier& a, const ActionIdentifier& b)
    {
        return a.m_Hash == b.m_Hash && std::strcmp(a.c_str(), b.c_str()) == 0;
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

    static constexpr size_t kMaxNameLength = 63; // 63 chars + null terminator

    char m_Name[kMaxNameLength + 1]{ 0 };
    uint64_t m_Hash{ 0 };
};

/// @brief Maps an action identifier to an input.
/// The application passes an array of these to InputMapper ctor.
struct ActionMapping
{
    /// @brief The unique identifier for the action.
    ActionIdentifier ActionId;
    /// @brief The input that triggers the action. This can be an InputButton or an InputAxis.
    std::variant<InputButton, InputAxis> Input;
    /// @brief The scale factor to apply to the input value when triggering the action.
    float Scale{ 1 };
};

/// @brief Maps input events (button presses, axis movements) to actions identified by
/// ActionIdentifier. If two or more input events are mapped to the same action, the action is
/// triggered if any of the mapped inputs are triggered. In such cases the action's value will
/// be set by the event that generates the maximum absolute value.
/// If two events generate the same absolute value, the event that is processed first will set the
/// action's value.  For example, if one event generates -3, and another generates 3, the action's
/// value will be set to -3 if that event is processed first.
///
/// To process input events, call BeginFrame() at the start of the frame, then call ProcessEvent()
/// for each SDL_Event, and finally call EndFrame() at the end of the frame. After EndFrame(),
/// call an Action() variant to check if an action was triggered and get its value.
class InputMapper
{
public:
    // SDL supports 5 mouse buttons (left, right, middle, X1, X2),
    // but button indexes begin at 1, so we allocate an array of 6
    // and ignore index zero.  See SDL/include/SDL3/SDL_mouse.h
    static constexpr size_t kMaxMouseButtons = 6;

    explicit InputMapper(const std::span<const ActionMapping> mappings);

    /// @brief Clears the state of all actions. This should be called when the application loses
    /// focus or is minimized to prevent actions from being triggered when the application regains
    /// focus.
    void Clear();

    /// @brief Begins a new frame. This should be called at the start of each frame before processing
    /// input events.
    void BeginFrame();

    /// @brief Processes an SDL_Event and updates the state of the mapped actions accordingly.
    void ProcessEvent(const SDL_Event& event);

    /// @brief Ends the current frame. This should be called at the end of each frame after
    /// processing all input events.
    void EndFrame();

    /// @brief Checks if the specified action was triggered during the current frame.
    /// Must not be called before EndFrame() is called.
    bool Action(const ActionIdentifier& actionId) const;

    bool Action(const ActionIdentifier& actionId, float& value) const;

private:
    struct ButtonActionMapping // NOLINT(cppcoreguidelines-pro-type-member-init)
    {
        InputButton Button;
        float Scale{ 1 };
        // Index into m_ActionStates for the action that this button mapping triggers.
        size_t ActionStateIndex{ 0 };
    };

    struct AxisActionMapping // NOLINT(cppcoreguidelines-pro-type-member-init)
    {
        InputAxis Axis;
        float Scale{ 1 };
        // Index into m_ActionStates for the action that this axis mapping triggers.
        size_t ActionStateIndex{ 0 };
    };

    // Represents the state of an action, including whether it was triggered and its value.
    struct ActionState
    {
        ActionIdentifier ActionId;
        bool Triggered{ false };
        float Value{ 0.0f };
    };

    // Tracks the current state of a button.
    struct ButtonState
    {
        unsigned PressCount{ 0 };
        unsigned ReleaseCount{ 0 };
        bool DownState{ false };

        bool IsPressed() const { return PressCount > 0; }
        bool IsReleased() const { return ReleaseCount > 0; }
        bool IsDown() const { return DownState; }
    };

    void TriggerAction(const ButtonActionMapping& mapping);

    void TriggerAction(const InputAxis& inputAxis, const float value);

    std::vector<ButtonActionMapping> m_ButtonActionMappings;
    std::vector<AxisActionMapping> m_AxisActionMappings;

    // Track button states for all keys and mouse buttons.  The index into the vector is the scancode
    // for keys and the button index for mouse buttons.
    std::vector<ButtonState> m_KeyStates;
    std::array<ButtonState, kMaxMouseButtons> m_MouseButtonStates{};

    // Current state of all registered actions.
    std::vector<ActionState> m_ActionStates;

    // Mouse move and wheel deltas this frame.
    Vec2f m_MouseDelta{ 0, 0 };
    Vec2f m_MouseWheelDelta{ 0, 0 };

    bool m_InFrame{false};
};