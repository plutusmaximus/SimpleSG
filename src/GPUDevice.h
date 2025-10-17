#pragma once

#include "RefCount.h"
#include "Error.h"
#include "Vertex.h"

#include <expected>
#include <string>

class VertexBufferResource
{
public:

    VertexBufferResource() {}

    virtual ~VertexBufferResource() = 0 {}

    virtual void* GetBuffer() = 0;  //DO NOT SUBMIT

    IMPLEMENT_REFCOUNT(VertexBufferResource);
};

class IndexBufferResource
{
public:

    IndexBufferResource() {}

    virtual ~IndexBufferResource() = 0 {}

    virtual void* GetBuffer() = 0;  //DO NOT SUBMIT

    IMPLEMENT_REFCOUNT(IndexBufferResource);
};

class TextureResource
{
public:

    TextureResource() {}

    virtual ~TextureResource() = 0 {}

    virtual void* GetTexture() = 0;  //DO NOT SUBMIT

    IMPLEMENT_REFCOUNT(TextureResource);
};

using VertexBuffer = RefPtr<VertexBufferResource>;
using IndexBuffer = RefPtr<IndexBufferResource>;
using Texture = RefPtr<TextureResource>;

class GPUDeviceResource
{
public:

    GPUDeviceResource() {}

    virtual ~GPUDeviceResource() = 0 {}

    template<int COUNT>
    std::expected<VertexBuffer, Error> CreateVertexBuffer(const Vertex(&vertices)[COUNT])
    {
        return CreateVertexBuffer(vertices, COUNT);
    }

    virtual std::expected<VertexBuffer, Error> CreateVertexBuffer(
        const Vertex* vertices,
        const unsigned vertexCount) = 0;

    template<int COUNT>
    std::expected<IndexBuffer, Error> CreateIndexBuffer(const uint16_t(&indices)[COUNT])
    {
        return CreateIndexBuffer(indices, COUNT);
    }

    virtual std::expected<IndexBuffer, Error> CreateIndexBuffer(
        const uint16_t* indices,
        const unsigned indexCount) = 0;

    virtual std::expected<Texture, Error> CreateTextureFromPNG(const std::string_view path) = 0;

    virtual void* GetDevice() = 0;  //DO NOT SUBMIT

    virtual void* GetWindow() = 0;  //DO NOT SUBMIT

    IMPLEMENT_REFCOUNT(GPUDeviceResource);
};

using GPUDevice = RefPtr<GPUDeviceResource>;