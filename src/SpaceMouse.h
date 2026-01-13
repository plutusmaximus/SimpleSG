#pragma once
#if 0
#include "Error.h"

#include <expected>

class SpaceMouse
{
public:

    static Result<SpaceMouse*> Create();

    // If Create() fails then this function can be used to
    // create a dummy space mouse that never receives input.
    // This enables dependent code to avoid branching on whether
    // a spacemouse exists.  Call Destroy() to destroy it.
    static SpaceMouse* CreateDummy();

    static void Destroy(SpaceMouse* spacemouse);

    void Update();

private:

    SpaceMouse()
    {
    }

    explicit SpaceMouse(std::vector<void*> handles)
        : m_Handles(handles)
    {
    }

    short tx, ty, tz;
    short rx, ry, rz;
    unsigned short buttons;

    std::vector<void*> m_Handles;

    IMPLEMENT_NON_COPYABLE(SpaceMouse);
};
#endif