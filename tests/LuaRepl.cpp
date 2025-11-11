#include "LuaRepl.h"

#include <iostream>
#include <thread>

#include "Error.h"

LuaRepl::LuaRepl()
{
    if (!everify(!sm_Singleton))
    {
        return;
    }

    sm_Singleton = this;

    m_LuaState = luaL_newstate();
    luaL_openlibs(m_LuaState);

    m_InputThread = std::thread(InputReader, std::ref(m_Queue));

    std::cout << "> ";
}

LuaRepl::~LuaRepl()
{
    if (this != sm_Singleton)
    {
        return;
    }

    lua_close(m_LuaState);

    m_InputThread.join();

    m_LuaState = nullptr;
}

void
LuaRepl::Update()
{
    if (!everify(this == sm_Singleton))
    {
        return;
    }

    eassert(m_LuaState);

    if (auto line = TryDequeue())
    {
        if (luaL_loadstring(m_LuaState, line.value().data()) || lua_pcall(m_LuaState, 0, 0, 0) != LUA_OK)
        {
            const std::string err = std::format("Error executing Lua code: {}", lua_tostring(m_LuaState, -1));
            std::cerr << err << std::endl;
        }

        std::cout << "> ";
    }
}

void
LuaRepl::ExportFunction(lua_CFunction func, const char* name)
{
    if (!everify(this == sm_Singleton))
    {
        return;
    }

    eassert(m_LuaState);

    lua_pushcfunction(m_LuaState, func);
    lua_setglobal(m_LuaState, name);
}

std::optional<std::string>
LuaRepl::TryDequeue()
{
    if (!m_Queue.Sem.try_acquire()) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_Queue.Mutex);
    std::string value = std::move(m_Queue.Queue.front());
    m_Queue.Queue.pop();
    return value;
}

void
LuaRepl::InputReader(Queue& queue)
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::lock_guard<std::mutex> lock(queue.Mutex);
        queue.Queue.push(std::move(line));
        queue.Sem.release();
    }
}
