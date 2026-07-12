#pragma once

#include "AssertHelper.h"
#include "Result.h"
#include "shaders/ShaderInterop.h"
#include "Vertex.h"

#include <webgpu/webgpu_cpp.h>

template<typename T>
class ValidGpuObject
{
public:
    ValidGpuObject() = delete;

    static Result<ValidGpuObject> Create(T gpuObject)
    {
        MLG_CHECKV(gpuObject, "Invalid GPU object");
        return ValidGpuObject(std::move(gpuObject));
    }

    T& Get() { return m_GpuObject; }
    const T& Get() const { return m_GpuObject; }

    T* operator->() { return &m_GpuObject; }
    const T* operator->() const { return &m_GpuObject; }

    friend bool operator==(const ValidGpuObject& a, const ValidGpuObject& b)
    {
        return a.m_GpuObject.Get() == b.m_GpuObject.Get();
    }

private:

    explicit ValidGpuObject(T gpuObject)
        : m_GpuObject(std::move(gpuObject))
    {
        MLG_ASSERT(m_GpuObject, "Invalid GPU object");
    }

    T m_GpuObject;
};

enum class SemanticBufferType
{
    Vertex,
    Index,
    Indirect,
    Uniform,
    Storage
};

/// @brief A strongly-typed GPU buffer that wraps a wgpu::Buffer and provides type-safe access to its contents.
template<typename T, SemanticBufferType BufferType>
class SemanticGpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "SemanticGpuBuffer can only be used with trivially copyable types");
    static_assert(!std::is_pointer_v<T>, "SemanticGpuBuffer can only be used with non-pointer types");
    static_assert(!std::is_reference_v<T>, "SemanticGpuBuffer can only be used with non-reference types");

public:
    using value_type = T;

    SemanticGpuBuffer() = delete;

    static Result<SemanticGpuBuffer> Create(wgpu::Device gpuDevice, wgpu::Buffer buffer)
    {
        MLG_CHECKV(gpuDevice, "Invalid wgpu::Device");
        MLG_CHECKV(buffer, "Invalid wgpu::Buffer");

        return SemanticGpuBuffer(std::move(gpuDevice), std::move(buffer));
    }

    const wgpu::Buffer& GetGpuBuffer() const { return m_GpuBuffer; }

    size_t BufferSize() const { return m_GpuBuffer.GetSize(); }

    size_t Count() const { return BufferSize() / sizeof(T); }

    // Stores a single value at the given index.
    void Store(std::size_t index, const T& value)
    {
        Store(index, std::span<const T>(&value, 1));
    }

    // Stores an array of values starting at the given index.
    void Store(std::size_t index, std::span<const T> values)
    {
        const size_t offset = index * sizeof(T);

        MLG_ASSERT((offset + (values.size() * sizeof(T))) <= BufferSize(), "Index out of bounds");

        m_Device.GetQueue().WriteBuffer(GetGpuBuffer(),
            offset,
            values.data(),
            values.size() * sizeof(T));
    }

    // Stores an array of values starting at the zero index.
    void Store(std::span<const T> values) { Store(0, values); }

    friend bool operator==(const SemanticGpuBuffer& a, const SemanticGpuBuffer& b)
    {
        return a.m_Device.Get() == b.m_Device.Get() && a.m_GpuBuffer.Get() == b.m_GpuBuffer.Get();
    }

private:

    SemanticGpuBuffer(wgpu::Device gpuDevice, wgpu::Buffer buffer)
        : m_Device(std::move(gpuDevice)), m_GpuBuffer(std::move(buffer))
    {
    }

    wgpu::Device m_Device{nullptr};
    wgpu::Buffer m_GpuBuffer{nullptr};
};

/// @brief Type traits to determine if a type is a SemanticGpuBuffer of a specific buffer type.
#define MLG_DEFINE_GPU_BUFFER_TYPE(typeName, bufferType) \
template<typename T>\
struct is_gpu_##typeName##_buffer_type : std::false_type {}; \
template<typename T> \
struct is_gpu_##typeName##_buffer_type<SemanticGpuBuffer<T, SemanticBufferType::bufferType>> : std::true_type {}; \
template<typename T> \
inline constexpr bool is_gpu_##typeName##_buffer_type_v = is_gpu_##typeName##_buffer_type<T>::value;

MLG_DEFINE_GPU_BUFFER_TYPE(vertex, Vertex)
MLG_DEFINE_GPU_BUFFER_TYPE(index, Index)
MLG_DEFINE_GPU_BUFFER_TYPE(indirect, Indirect)
MLG_DEFINE_GPU_BUFFER_TYPE(uniform, Uniform)
MLG_DEFINE_GPU_BUFFER_TYPE(storage, Storage)

using ValidTexture = ValidGpuObject<wgpu::Texture>;
using ValidBindGroupLayout = ValidGpuObject<wgpu::BindGroupLayout>;
using ValidBindGroup = ValidGpuObject<wgpu::BindGroup>;

// Strongly-typed GPU storage buffer classes.
using VertexBuffer = SemanticGpuBuffer<Vertex, SemanticBufferType::Vertex>;
using IndexBuffer = SemanticGpuBuffer<VertexIndex, SemanticBufferType::Index>;
using DrawIndirectBuffer = SemanticGpuBuffer<ShaderInterop::DrawIndirectParams, SemanticBufferType::Indirect>;
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform, SemanticBufferType::Storage>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform, SemanticBufferType::Storage>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties, SemanticBufferType::Storage>;
using CameraParamsBuffer = SemanticGpuBuffer<ShaderInterop::CameraParams, SemanticBufferType::Uniform>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants, SemanticBufferType::Storage>;