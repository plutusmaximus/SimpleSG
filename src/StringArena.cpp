#include "StringArena.h"

#include "AssertHelper.h"

size_t StringArena::sm_TotalStringSize;
size_t StringArena::sm_ChunkCount;
size_t StringArena::sm_StringCount;

StringArena::StringArena(const size_t chunkSize)
    : m_ChunkSize(chunkSize)
{
}

StringHandle
StringArena::NewString(const std::string_view& stringView)
{
    const size_t stringSizeWithNull = stringView.size() + 1;

    MLG_ABORTIF(stringSizeWithNull > m_ChunkSize, "String size exceeds chunk size");

    if(m_Chunks.empty() || m_Chunks.back().Buffer.size() + stringSizeWithNull > m_ChunkSize)
    {
        m_Chunks.emplace_back(m_ChunkSize);
        ++sm_ChunkCount;
    }

    Chunk& chunk = m_Chunks.back();
    const size_t offset = chunk.Buffer.size();
    chunk.Buffer.insert(chunk.Buffer.end(), stringView.begin(), stringView.end());
    chunk.Buffer.push_back('\0');

    const char* ptr= &chunk.Buffer[offset];

    ++sm_StringCount;
    sm_TotalStringSize += stringSizeWithNull;

    return StringHandle(std::string_view(ptr, stringView.size()));
}