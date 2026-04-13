#pragma once

#include "Result.h"

template<typename T> class Vec2;
using Vec2f = Vec2<float>;
class Point;

class Application
{
public:

    virtual ~Application() = default;

    virtual Result<> Initialize() = 0;

    virtual void Shutdown() = 0;

    virtual void Update(const float deltaSeconds) = 0;

    virtual bool IsRunning() const = 0;

    virtual void OnKeyDown(const int /*keyCode*/) {}
    virtual void OnKeyUp(const int /*keyCode*/) {}
    virtual void OnMouseMove(const Vec2f& /*mouseDelta*/) {}
    virtual void OnMouseDown(const Point& /*mouseLoc*/, const int /*mouseButton*/) {}
    virtual void OnMouseUp(const int /*mouseButton*/) {}
    virtual void OnScroll(const Vec2f& /*scroll*/) {}
    virtual void OnFocusGained() {}
    virtual void OnFocusLost() {}
    virtual void OnResize(const int /*width*/, const int /*height*/) {}
};