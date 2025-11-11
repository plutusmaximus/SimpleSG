#pragma once

#include <queue>
#include <mutex>
#include <semaphore>
#include <optional>
#include <string>

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

class LuaRepl
{
public:

    LuaRepl();

    ~LuaRepl();

    void Update();

    void ExportFunction(lua_CFunction func, const char* name);

private:

    struct Queue
    {
        mutable std::mutex Mutex;
        std::queue<std::string> Queue;
        std::counting_semaphore<> Sem{ 0 };
    };

    std::optional<std::string> TryDequeue();

    static void InputReader(Queue& queue);

    lua_State* m_LuaState{ nullptr };
    std::thread m_InputThread;

    Queue m_Queue;

    static inline LuaRepl* sm_Singleton;
};