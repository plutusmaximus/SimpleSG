#pragma once

#include "Result.h"

#include <string>
#include <memory>

class Level;
struct lua_State;

class LuaRuntime
{
public:

    static constexpr const char* kLevlLibName = "level";

    LuaRuntime() = delete;
    ~LuaRuntime() = default;
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&& other) = delete;
    LuaRuntime& operator=(LuaRuntime&& other) = delete;

    explicit LuaRuntime(Level* level);

    lua_State* GetLuaState() const { return m_LuaState.get(); }

    Level* GetLevel() const { return m_Level; }

    Result<std::string> Eval(const std::string_view code) const;

private:

    enum class UpvalueIndex : int
    {
        Level = 1
    };

    static int Open(lua_State* L);

    static Level* GetLevel(lua_State* L);

    static void CloseLuaState(lua_State* L);

    static int CountNodes(lua_State* L);

    Level* m_Level{ nullptr };
    std::unique_ptr<lua_State, decltype(&CloseLuaState)> m_LuaState;
};