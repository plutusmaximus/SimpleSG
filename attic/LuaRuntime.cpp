#include "LuaRuntime.h"

#include "Level.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

LuaRuntime::LuaRuntime(Level* level)
    : m_Level(level),
      m_LuaState(luaL_newstate(), &CloseLuaState)
{
    lua_State* L = m_LuaState.get();
    luaL_openlibs(L);
    lua_getglobal(L, "package");    // package
    lua_getfield(L, -1, "preload"); // package, package.preload

    lua_pushlightuserdata(L, level); // package, preload, level*
    lua_pushcclosure(L, Open, 1);    // package, preload, opener

    lua_setfield(L, -2, kLevlLibName); // preload["level"] = opener

    lua_pop(L, 2); // pop preload and package
}

Result<std::string>
LuaRuntime::Eval(const std::string_view code) const
{
    lua_State* L = m_LuaState.get();

    const int stackTop = lua_gettop(L);

    const int status =
        luaL_loadbuffer(L, code.data(), code.size(), "eval") || lua_pcall(L, 0, LUA_MULTRET, 0);

    if(status != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        MLG_ERROR(err);
        lua_pop(L, 1);
        return std::string(err);
    }

    std::string resultStr;

    if(lua_gettop(L) > stackTop)
    {
        const char* result = lua_tostring(L, -1);
        resultStr = result ? result : "";
        lua_pop(L, 1);
    }

    return resultStr;
}

int
LuaRuntime::Open(lua_State* L)
{
    Level* level = GetLevel(L);

    const luaL_Reg mylibFuncs[] //
        { { .name = "countNodes", .func = CountNodes }, { .name = nullptr, .func = nullptr } };

    const int funcCount = static_cast<int>(std::size(mylibFuncs));
    lua_createtable(L, 0, funcCount);
    lua_pushlightuserdata(L, level);
    luaL_setfuncs(L, mylibFuncs, 1); // NOLINT

    return 1;
}

Level*
LuaRuntime::GetLevel(lua_State* L)
{
    return static_cast<Level*>(
        lua_touserdata(L, lua_upvalueindex(static_cast<int>(UpvalueIndex::Level))));
}

void
LuaRuntime::CloseLuaState(lua_State* L)
{
    if(L)
    {
        lua_close(L);
    }
}

int
LuaRuntime::CountNodes(lua_State* L)
{
    const Level* level = GetLevel(L);

    const lua_Integer count = static_cast<lua_Integer>(level->GetAllNodes().size());
    lua_pushinteger(L, count);

    return 1;
}