#pragma once

#include "VecMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <variant>
#include <vector>

/// Represents the device to which an input button belongs.
// Devices like gamepads, etc. can be added as they are implemented.
enum class InputButtonDevice
{
    Keyboard,
    Mouse,
};

/// Represents the device to which an input axis belongs.
/// Devices like gamepads, etc. can be added as they are implemented.
enum class InputAxisDevice
{
    Mouse,
    MouseWheel
};

/// Represents input button condition (pressed, released, held) that
/// triggers an action.
enum class InputButtonCondition
{
    // Button pressed this frame.
    Pressed,
    // Button released this frame.
    Released,
    // Button is being held down.
    Held
};

/// Represents the identifier of an input axis (X, Y, Z) that can be mapped to an action.
enum class InputAxisIdentifier
{
    X,
    Y,
    Z
};

/// Represents a specific input button and its condition (pressed, released, held).
/// Used to map input button events to actions.
class InputButton
{
public:
    InputButton() = delete;

    constexpr InputButton(
        const InputButtonDevice device, const unsigned buttonId, const InputButtonCondition condition)
        : m_Device(device),
          m_ButtonId(buttonId),
          m_Condition(condition)
    {
    }

    constexpr InputButtonDevice GetDevice() const { return m_Device; }
    constexpr unsigned GetId() const { return m_ButtonId; }
    constexpr InputButtonCondition GetCondition() const { return m_Condition; }

    constexpr bool TriggersOnPress() const { return m_Condition == InputButtonCondition::Pressed; }
    constexpr bool TriggersOnRelease() const { return m_Condition == InputButtonCondition::Released; }
    constexpr bool TriggersWhileHeld() const { return m_Condition == InputButtonCondition::Held; }

    friend constexpr bool operator==(const InputButton& a, const InputButton& b) = default;

    /// Helper functions to create InputButton instances for specific button conditions.

    static constexpr InputButton KeyPressed(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonCondition::Pressed);
    }

    static constexpr InputButton KeyReleased(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonCondition::Released);
    }

    static constexpr InputButton KeyHeld(const unsigned keyCode)
    {
        return InputButton(InputButtonDevice::Keyboard, keyCode, InputButtonCondition::Held);
    }

    static constexpr InputButton MousePressed(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonCondition::Pressed);
    }

    static constexpr InputButton MouseReleased(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonCondition::Released);
    }

    static constexpr InputButton MouseHeld(const unsigned buttonCode)
    {
        return InputButton(InputButtonDevice::Mouse, buttonCode, InputButtonCondition::Held);
    }

private:
    InputButtonDevice m_Device;
    unsigned m_ButtonId;
    InputButtonCondition m_Condition;
};

/// Represents a specific input axis and its direction (X, Y, Z).
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

    static constexpr InputAxis MouseMoveX()
    {
        static constexpr InputAxis instance(InputAxisDevice::Mouse, InputAxisIdentifier::X);
        return instance;
    }
    static constexpr InputAxis MouseMoveY()
    {
        static constexpr InputAxis axis(InputAxisDevice::Mouse, InputAxisIdentifier::Y);
        return axis;
    }
    static constexpr InputAxis MouseMoveZ()
    {
        static constexpr InputAxis axis(InputAxisDevice::Mouse, InputAxisIdentifier::Z);
        return axis;
    }
    static constexpr InputAxis MouseWheelX()
    {
        static constexpr InputAxis axis(InputAxisDevice::MouseWheel, InputAxisIdentifier::X);
        return axis;
    }
    static constexpr InputAxis MouseWheelY()
    {
        static constexpr InputAxis axis(InputAxisDevice::MouseWheel, InputAxisIdentifier::Y);
        return axis;
    }

private:
    InputAxisDevice m_Device;
    InputAxisIdentifier m_AxisId;
};

/// Represents a unique identifier for an action that can be mapped to input events.
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

/// Represents an input (button, axis, etc.) that can trigger an action.
class InputTrigger
{
public:
    enum class Type
    {
        Button,
        Axis
    };

    InputTrigger() = delete;

    InputTrigger(const InputButton& button) // NOLINT(google-explicit-constructor)
        : m_Trigger(button)
    {
    }

    InputTrigger(const InputAxis& axis) // NOLINT(google-explicit-constructor)
        : m_Trigger(axis)
    {
    }

    Type GetType() const
    {
        return std::holds_alternative<InputButton>(m_Trigger) ? Type::Button : Type::Axis;
    }

    const InputButton& GetButton() const
    {
        MLG_ASSERT(GetType() == Type::Button, "InputTrigger does not hold an InputButton");
        return std::get<InputButton>(m_Trigger);
    }

    const InputAxis& GetAxis() const
    {
        MLG_ASSERT(GetType() == Type::Axis, "InputTrigger does not hold an InputAxis");
        return std::get<InputAxis>(m_Trigger);
    }

private:
    std::variant<InputButton, InputAxis> m_Trigger;
};

/// Maps an action identifier to an input.
/// The application passes an array of these to InputMapper ctor.
struct ActionMapping
{
    /// The unique identifier for the action.
    ActionIdentifier ActionId;
    /// The input that triggers the action. This can be an InputButton or an InputAxis.
    InputTrigger Trigger;
    /// The scale factor to apply to the input value when triggering the action.
    float Scale{ 1 };
};

/// Maps input events (button presses, axis movements) to actions identified by
/// ActionIdentifier. If two or more input events are mapped to the same action, the action is
/// triggered if any of the mapped inputs occur. In such cases the action's value will
/// be set by the event that generates the maximum absolute value.
///
/// To process input events, call BeginFrame() at the start of the frame, then call OnButtonPressed(),
/// OnButtonReleased(), etc., for each input event, and finally call EndFrame() at the end of the frame.
/// After EndFrame(), call an IsActionTriggered() variant to check if an action was triggered and get its value.
class InputMapper
{
public:
    // SDL supports 5 mouse buttons (left, right, middle, X1, X2),
    // but button indexes begin at 1, so we allocate an array of 6
    // and ignore index zero.  See SDL/include/SDL3/SDL_mouse.h
    static constexpr size_t kMaxMouseButtons = 6;

    // SDL_SCANCODE_COUNT
    static constexpr size_t kMaxKeyButtons = 512;

    InputMapper() = default;
    ~InputMapper() = default;
    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;
    InputMapper(InputMapper&&) = default;
    InputMapper& operator=(InputMapper&&) = default;

    explicit InputMapper(const std::span<const ActionMapping> mappings);

    /// Clears the state of all actions. This should be called when the application loses
    /// focus or is minimized to prevent actions from being triggered when the application regains
    /// focus.
    void Clear();

    /// Begins a new frame. This should be called at the start of each frame before processing
    /// input events.
    void BeginFrame();

    void OnButtonPressed(const InputButtonDevice device, const unsigned buttonId);
    void OnButtonReleased(const InputButtonDevice device, const unsigned buttonId);
    void OnAxis(const InputAxisDevice device, const InputAxisIdentifier axisId, const float value);

    /// Ends the current frame. This should be called at the end of each frame after
    /// processing all input events.
    void EndFrame();

    /// Checks if the specified action was triggered during the current frame.
    /// Must not be called before EndFrame() is called.
    bool IsActionTriggered(const ActionIdentifier& actionId) const;

    /// Checks if the specified action was triggered during the current frame,
    /// and retrieves its value if it was triggered.
    /// Must not be called before EndFrame() is called.
    bool IsActionTriggered(const ActionIdentifier& actionId, float& value) const;

private:
    // Represents the state of an action, including whether it was triggered and its value.
    struct ActionState
    {
        ActionIdentifier ActionId;
        bool Triggered{ false };
        float Value{ 0.0f };
    };

    struct ButtonActionMapping // NOLINT(cppcoreguidelines-pro-type-member-init)
    {
        InputButton Button;
        float Scale{ 1 };
        ActionState* m_ActionState{ nullptr };
    };

    struct AxisActionMapping // NOLINT(cppcoreguidelines-pro-type-member-init)
    {
        InputAxis Axis;
        float Scale{ 1 };
        ActionState* m_ActionState{ nullptr };
    };

    // Tracks the current state of a button.
    struct ButtonState
    {
        unsigned PressCount{ 0 };
        unsigned ReleaseCount{ 0 };
        bool HeldState{ false };

        bool IsPressed() const { return PressCount > 0; }
        bool IsReleased() const { return ReleaseCount > 0; }
        bool IsHeld() const { return HeldState; }
    };

    static void TriggerAction(const ButtonActionMapping& mapping);

    void TriggerAction(const InputAxis& inputAxis, const float value);

    ActionState* GetActionState(const ActionIdentifier& actionId);

    std::vector<ButtonActionMapping> m_ButtonActionMappings;
    std::vector<AxisActionMapping> m_AxisActionMappings;

    // Track button states for all keys and mouse buttons.  The index into the vector is the
    // scancode for keys and the button index for mouse buttons.
    std::array<ButtonState, kMaxKeyButtons> m_KeyStates;
    std::array<ButtonState, kMaxMouseButtons> m_MouseButtonStates{};

    // Current state of all registered actions.
    std::vector<ActionState> m_ActionStates;

    // Mouse move and wheel deltas this frame.
    Vec3f m_MouseDelta{ 0, 0, 0 };
    Vec3f m_MouseWheelDelta{ 0, 0, 0 };

    bool m_InFrame{ false };
};