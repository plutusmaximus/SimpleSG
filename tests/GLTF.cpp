#define _CRT_SECURE_NO_WARNINGS

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <stb_image.h>

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "FileIo.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <ranges>
#include <stack>
#include <thread>
#include <type_traits>
#include <unordered_map>

// TODO
// * Handle materials that don't have PBR metallic-roughness properties
// * Handle materials that don't have a base color texture
// * Option to treat failed primitive/material loading as fatal.
// * Add FileIo::FetchRange()

static constexpr const char* APP_NAME = "Space Rocks";

static constexpr int kMaxTexCoords = 6;
static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr wgpu::TextureFormat kColorTargetFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;

namespace mlg
{
template<typename T>
using Result2 = std::optional<T>;

struct Position
{
    float x, y, z;
};

struct Normal
{
    float x, y, z;
};

struct TexCoord
{
    float u, v;
};

struct Color
{
    float r, g, b, a;
};

template<typename F>
class Defer
{
public:
    explicit Defer(F&& f)
        : m_Func(std::forward<F>(f))
    {
    }
    ~Defer()
    {
        if(!m_Cancelled)
        {
            m_Func();
        }
    }

    void Cancel() { m_Cancelled = true; }

private:
    F m_Func;
    bool m_Cancelled{ false };
};

class Log final
{
    static inline std::vector<std::string> s_LogPrefixStack;
    static inline std::string s_LogPrefix;

public:
    static void MakePrefix()
    {
        s_LogPrefix = "[";

        int count = 0;

        for(const auto& prefix : s_LogPrefixStack)
        {
            if(count > 0)
            {
                s_LogPrefix += ": ";
            }
            s_LogPrefix += prefix;
            ++count;
        }

        s_LogPrefix += "] ";
    }

    template<typename... Args>
    static void PushPrefix(std::format_string<Args...> fmt, Args&&... args)
    {
        s_LogPrefixStack.push_back(std::format(fmt, std::forward<Args>(args)...));
        MakePrefix();
    }

    static void PopPrefix()
    {
        if(!s_LogPrefixStack.empty())
        {
            s_LogPrefixStack.pop_back();
            MakePrefix();
        }
    }

    template<typename... Args>
    static inline void LogTo(std::ostream& stream, const std::string& prefix, std::format_string<Args...> fmt, Args&&... args)
    {
        stream << prefix << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    }

    static inline void LogTo(std::ostream& stream, const std::string& prefix, std::string_view msg)
    {
        stream << prefix << msg << std::endl;
    }

    static inline void LogTo() {}

    template<typename... Args>
    static inline void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        LogTo(std::cerr, "[ERR] " + s_LogPrefix, fmt, std::forward<Args>(args)...);
    }

    static inline void Error(std::string_view msg)
    {
        LogTo(std::cerr, "[ERR] " + s_LogPrefix, msg);
    }

    static inline void Error() {}

    template<typename... Args>
    static inline void Debug(std::format_string<Args...> fmt, Args&&... args)
    {
        LogTo(std::cout, "[DBG] " + s_LogPrefix, fmt, std::forward<Args>(args)...);
    }

    static inline void Debug(std::string_view msg)
    {
        LogTo(std::cout, "[DBG] " + s_LogPrefix, msg);
    }

    static inline void Debug() {}
};

struct LogScope
{
    template<typename... Args>
    LogScope(std::format_string<Args...> fmt, Args&&... args)
    {
        Log::PushPrefix(fmt, std::forward<Args>(args)...);
    }

    ~LogScope()
    {
        Log::PopPrefix();
    }
};

#define MLG_CHECK(expr, ...) \
    do { \
        if (!(expr)) { \
            mlg::Log::Error(__VA_ARGS__); \
            return {}; \
        } \
    } while (0)

// A range of FileIo::AsyncToken objects.
template <class R>
concept AsyncTokenRange =
    std::ranges::input_range<R> && // “works with range-for” (at least input iteration)
    std::same_as<
        std::remove_cvref_t<std::ranges::range_reference_t<R>>,
        FileIo::AsyncToken
    >;

// Wait for all async tokens in the range to complete.
template <AsyncTokenRange R>
static void WaitForPendingLoads(const R& asyncTokens)
{
    bool pending = true;
    while(pending)
    {
        pending = false;
        for (const auto& token : asyncTokens)
        {
            if (FileIo::IsPending(token))
            {
                pending = true;
                break;
            }
        }
        std::this_thread::yield();
    }
}

class Gltf final
{
public:

    struct Material
    {
        std::string Name;
        std::string BaseTextureUri;
        std::string MetallicRoughnessTextureUri;
        Color BaseColor{ 0, 0, 0, 0 };
        float MetallicFactor{ 0 };
        float RoughnessFactor{ 0 };
        bool DoubleSided{ false };
    };

    struct Primitive
    {
        std::vector<uint32_t> Indices;
        std::span<const Position> Positions;
        std::span<const Normal> Normals;
        std::span<const TexCoord> TexCoords[kMaxTexCoords];

        Material Mtl;
    };

    static inline const char*
    cgltf_result_to_string(cgltf_result r)
    {
        switch (r)
        {
            case cgltf_result_success:              return "success";
            case cgltf_result_data_too_short:       return "data_too_short";
            case cgltf_result_unknown_format:       return "unknown_format";
            case cgltf_result_invalid_json:         return "invalid_json";
            case cgltf_result_invalid_gltf:         return "invalid_gltf";
            case cgltf_result_invalid_options:      return "invalid_options";
            case cgltf_result_file_not_found:       return "file_not_found";
            case cgltf_result_io_error:             return "io_error";
            case cgltf_result_out_of_memory:        return "out_of_memory";
            case cgltf_result_legacy_gltf:          return "legacy_gltf";
            default:                                return "unknown";
        }
    }

    static inline const char*
    cgltf_attribute_type_to_string(cgltf_attribute_type type)
    {
        switch (type)
        {
            case cgltf_attribute_type_position:     return "position";
            case cgltf_attribute_type_normal:       return "normal";
            case cgltf_attribute_type_tangent:      return "tangent";
            case cgltf_attribute_type_texcoord:     return "texcoord";
            case cgltf_attribute_type_color:        return "color";
            case cgltf_attribute_type_joints:       return "joints";
            case cgltf_attribute_type_weights:      return "weights";
            default:                                return "unknown";
        }
    }

    static Result2<bool> LoadGLTF(const std::filesystem::path& gltfFilePath, cgltf_data*& outData)
    {
        auto fetchResult = FileIo::Fetch(gltfFilePath.string());
        MLG_CHECK(fetchResult,
            "Failed to fetch glTF file {}: {}",
            gltfFilePath.string(),
            fetchResult.error());
        auto fetchToken = *fetchResult;

        WaitForPendingLoads(std::views::single(fetchToken));

        auto resultData = FileIo::GetResult(fetchToken);
        MLG_CHECK(resultData,
            "Failed to get glTF file {}: {}",
            gltfFilePath.string(),
            resultData.error());

        cgltf_options options{};
        cgltf_data* data = NULL;
        cgltf_result parseResult = cgltf_parse(&options, resultData->data(), resultData->size(), &data);
        MLG_CHECK(parseResult == cgltf_result_success,
            "Failed to parse glTF file: {}",
            cgltf_result_to_string(parseResult));

        outData = data;
        return true;
    }

    static Result2<bool> LoadBuffers(const cgltf_data *data, const std::filesystem::path &parentPath, std::unordered_map<std::string, FileIo::FetchData> &bufferData)
    {
        bufferData.clear();
        bufferData.reserve(data->buffers_count);

        std::unordered_map<std::string, FileIo::AsyncToken> asyncTokens;
        asyncTokens.reserve(data->buffers_count);

        for (cgltf_size i = 0; i < data->buffers_count; ++i)
        {
            cgltf_buffer* buffer = &data->buffers[i];
            auto result = FileIo::Fetch((parentPath / buffer->uri).string());

            MLG_CHECK(result, "Failed to fetch buffer {}: {}", buffer->uri, result.error());

            asyncTokens.emplace(buffer->uri, *result);
        }

        auto tokensView = std::views::values(asyncTokens);
        WaitForPendingLoads(tokensView);

        for (const auto& [uri, token] : asyncTokens)
        {
            auto result = FileIo::GetResult(token);
            MLG_CHECK(result, "Failed to get buffer {}: {}", uri, result.error());
            bufferData.emplace(uri, std::move(*result));
        }
        return true;
    }

    static Result2<const uint8_t*>
    GetBufferData(const cgltf_attribute& attribute,
        const std::unordered_map<std::string, FileIo::FetchData>& bufferData)
    {
        const cgltf_accessor& accessor = *attribute.data;
        MLG_CHECK(!accessor.is_sparse, "{}:{}:{} is sparse, which is not supported yet",
                            attribute.name ? attribute.name : "<unnamed attribute>",
                            accessor.name ? accessor.name : "<unnamed accessor>",
                            attribute.data->buffer_view->buffer->uri);

        const cgltf_buffer_view& bufferView = *accessor.buffer_view;
        const cgltf_buffer& buffer = *bufferView.buffer;
        auto it = bufferData.find(buffer.uri);
        MLG_CHECK(it != bufferData.end(), "Buffer {} not found", buffer.uri);
        return it->second.data() + bufferView.offset + accessor.offset;
    }

    static Result2<bool> LoadTexture(const cgltf_texture& texture, std::string& uri)
    {
        uri.clear();

        MLG_CHECK(texture.image, "Texture image is not set");
        MLG_CHECK(texture.image->uri, "Texture image URI is not set");
        uri = texture.image->uri;

        MLG_CHECK(!uri.empty(), "Texture URI is empty");

        return true;
    }

    static Result2<bool> LoadBaseTexture(const cgltf_texture_view& textureView, Material& outMaterial)
    {
        if(!textureView.texture)
        {
            return true;
        }

        MLG_CHECK(LoadTexture(*textureView.texture, outMaterial.BaseTextureUri));

        return true;
    }

    static Result2<bool> LoadMetallicRoughnessTexture(const cgltf_texture_view& textureView, Material& outMaterial)
    {
        if(!textureView.texture)
        {
            return true;
        }

        MLG_CHECK(LoadTexture(*textureView.texture, outMaterial.MetallicRoughnessTextureUri));
        return true;
    }

    static Result2<bool> LoadMaterial(const cgltf_material& material, Material& outMaterial)
    {
        LogScope attributeScope("mtrl {}", material.name ? material.name : "<unnamed material>");

        outMaterial.Name = material.name ? material.name : "<unnamed material>";
        outMaterial.DoubleSided = material.double_sided;

        MLG_CHECK(material.has_pbr_metallic_roughness,
            "Material does not have PBR metallic-roughness");

        const cgltf_texture_view& baseTexture = material.pbr_metallic_roughness.base_color_texture;

        MLG_CHECK(LoadBaseTexture(baseTexture, outMaterial));

        const cgltf_texture_view& metallicRoughnessTexture = material.pbr_metallic_roughness.metallic_roughness_texture;

        MLG_CHECK(LoadMetallicRoughnessTexture(metallicRoughnessTexture, outMaterial));

        outMaterial.MetallicFactor = material.pbr_metallic_roughness.metallic_factor;
        outMaterial.RoughnessFactor = material.pbr_metallic_roughness.roughness_factor;

        const cgltf_float (&baseColorFactor)[4] = material.pbr_metallic_roughness.base_color_factor;

        outMaterial.BaseColor.r = baseColorFactor[0];
        outMaterial.BaseColor.g = baseColorFactor[1];
        outMaterial.BaseColor.b = baseColorFactor[2];
        outMaterial.BaseColor.a = baseColorFactor[3];

        return true;
    }

    static Result2<bool> LoadIndices(const cgltf_primitive& primitive,
        const std::unordered_map<std::string, FileIo::FetchData>& bufferData,
        std::vector<uint32_t>& outIndices)
    {
        outIndices.clear();

        if(!primitive.indices)
        {
            for(cgltf_size i = 0; i < primitive.attributes_count; ++i)
            {
                const cgltf_attribute& attribute = primitive.attributes[i];
                if(attribute.type == cgltf_attribute_type_position)
                {
                    for(cgltf_size j = 0; j < attribute.data->count; ++j)
                    {
                        outIndices.push_back(static_cast<uint32_t>(j));
                    }
                }
            }

            MLG_CHECK(!outIndices.empty(), "Failed to generate indices for primitive without indices");

            return true;
        }

        const cgltf_accessor& accessor = *primitive.indices;
        MLG_CHECK(!accessor.is_sparse, "Sparse indices are not supported");

        const cgltf_buffer_view& bufferView = *accessor.buffer_view;
        const cgltf_buffer& buffer = *bufferView.buffer;
        auto it = bufferData.find(buffer.uri);
        MLG_CHECK(it != bufferData.end(), "Buffer {} not found", buffer.uri);

        const uint8_t* bufferDataPtr = it->second.data() + bufferView.offset + accessor.offset;

        outIndices.reserve(accessor.count);

        switch(accessor.component_type)
        {
            case cgltf_component_type_r_8:
            case cgltf_component_type_r_8u:
            {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(bufferDataPtr);
                for(cgltf_size i = 0; i < accessor.count; ++i)
                {
                    outIndices.push_back(static_cast<uint32_t>(data[i]));
                }
            }
            break;
            case cgltf_component_type_r_16:
            case cgltf_component_type_r_16u:
            {
                const uint16_t* data = reinterpret_cast<const uint16_t*>(bufferDataPtr);
                for(cgltf_size i = 0; i < accessor.count; ++i)
                {
                    outIndices.push_back(static_cast<uint32_t>(data[i]));
                }
            }
            break;
            case cgltf_component_type_r_32u:
            {
                const uint32_t* data = reinterpret_cast<const uint32_t*>(bufferDataPtr);
                for(cgltf_size i = 0; i < accessor.count; ++i)
                {
                    outIndices.push_back(static_cast<uint32_t>(data[i]));
                }
            }
            break;
            default:
                MLG_CHECK(false,
                    "Unsupported index component type {}",
                    std::to_underlying(accessor.component_type));
        }

        return true;
    }

    template<cgltf_attribute_type AttrType>
    struct AttributeTraits;

    template<>
    struct AttributeTraits<cgltf_attribute_type_position>
    {
        using type = Position;
        static constexpr cgltf_component_type component_type = cgltf_component_type_r_32f;
    };
    template<>
    struct AttributeTraits<cgltf_attribute_type_normal>
    {
        using type = Normal;
        static constexpr cgltf_component_type component_type = cgltf_component_type_r_32f;
    };
    template<>
    struct AttributeTraits<cgltf_attribute_type_texcoord>
    {
        using type = TexCoord;
        static constexpr cgltf_component_type component_type = cgltf_component_type_r_32f;
    };

    template<cgltf_attribute_type AttrType>
    static Result2<bool>
    LoadAttributes(const cgltf_attribute& attribute,
        const std::unordered_map<std::string, FileIo::FetchData>& bufferData,
        std::span<const typename AttributeTraits<AttrType>::type>& outData)
    {
        MLG_CHECK(attribute.data->component_type == AttributeTraits<AttrType>::component_type,
            "Attribute {} has unsupported component type {}",
            attribute.name ? attribute.name : "<unnamed attribute>",
            std::to_underlying(attribute.data->component_type));

        auto bufferDataPtr = GetBufferData(attribute, bufferData);
        MLG_CHECK(bufferDataPtr,
            "Failed to get buffer data for attribute {}",
            attribute.name ? attribute.name : "<unnamed attribute>");

        const typename AttributeTraits<AttrType>::type* data =
            reinterpret_cast<const typename AttributeTraits<AttrType>::type*>(*bufferDataPtr);

        outData =
            std::span<const typename AttributeTraits<AttrType>::type>(data, attribute.data->count);

        return true;
    }

    static Result2<bool> LoadPrimitive(const cgltf_primitive& primitive,
        const std::unordered_map<std::string, FileIo::FetchData>& bufferData,
        Primitive& outAttributes)
    {
        MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
            "Only triangle primitives are supported");

        MLG_CHECK(primitive.material, "Primitive does not have a material");

        MLG_CHECK(primitive.attributes_count > 0,
            "Primitive does not have any attributes");

        MLG_CHECK(primitive.targets_count == 0, "Morph targets are not supported");

        MLG_CHECK(!primitive.has_draco_mesh_compression,
            "Draco mesh compression is not supported");

        MLG_CHECK(LoadMaterial(*primitive.material, outAttributes.Mtl));

        MLG_CHECK(LoadIndices(primitive, bufferData, outAttributes.Indices));

        const std::span<const cgltf_attribute> attributes(primitive.attributes, primitive.attributes_count);

        for(cgltf_size attributeIdx = 0; attributeIdx < attributes.size(); ++attributeIdx)
        {
            LogScope attributeScope("attr {}", attributeIdx);

            const cgltf_attribute& attribute = attributes[attributeIdx];

            switch(attribute.type)
            {
                case cgltf_attribute_type_position:
                    MLG_CHECK(LoadAttributes<cgltf_attribute_type_position>(attribute,
                            bufferData,
                            outAttributes.Positions));
                    break;

                case cgltf_attribute_type_normal:
                    MLG_CHECK(LoadAttributes<cgltf_attribute_type_normal>(attribute,
                            bufferData,
                            outAttributes.Normals));
                    break;

                case cgltf_attribute_type_texcoord:
                    MLG_CHECK(attribute.index < kMaxTexCoords,
                        "Texture coordinate index {} exceeds maximum supported {}",
                        attribute.index,
                        kMaxTexCoords);

                    MLG_CHECK(LoadAttributes<cgltf_attribute_type_texcoord>(attribute,
                            bufferData,
                            outAttributes.TexCoords[attribute.index]));
                    break;

                default:
                    Log::Error("Unsupported attribute type \"{}\"/{}",
                        cgltf_attribute_type_to_string(attribute.type),
                        std::to_underlying(attribute.type));
                    break;
            }
        }

        return true;
    }

    static Result2<bool> Load(const char* path)
    {
        const std::filesystem::path gltfFilePath{path};
        const std::filesystem::path parentPath = gltfFilePath.parent_path();

        cgltf_data* data = nullptr;
        MLG_CHECK(LoadGLTF(gltfFilePath, data));
        std::unordered_map<std::string, FileIo::FetchData> bufferData;

        MLG_CHECK(LoadBuffers(data, parentPath, bufferData));

        std::vector<std::string> textureUris;
        textureUris.reserve(data->textures_count);
        for(cgltf_size texIdx = 0; texIdx < data->textures_count; ++texIdx)
        {
            const cgltf_texture& texture = data->textures[texIdx];
            MLG_CHECK(LoadTexture(texture, textureUris.emplace_back()));
        }

        std::vector<FileIo::AsyncToken> textureFetchTokens;
        textureFetchTokens.reserve(textureUris.size());
        for(const auto& uri : textureUris)
        {
            Log::Debug("Fetching texture: {}", (parentPath / uri).string());

            auto result = FileIo::Fetch((parentPath / uri).string());
            MLG_CHECK(result, result.error().GetMessage().c_str());
            textureFetchTokens.emplace_back(std::move(*result));
        }

        WaitForPendingLoads(textureFetchTokens);

        std::vector<Material> materials;
        materials.reserve(data->materials_count);
        for(cgltf_size mtlIdx = 0; mtlIdx < data->materials_count; ++mtlIdx)
        {
            const cgltf_material& material = data->materials[mtlIdx];
            LoadMaterial(material, materials.emplace_back());
        }

        size_t primitiveCount = 0;
        for(cgltf_size meshIdx = 0; meshIdx < data->meshes_count; ++meshIdx)
        {
            primitiveCount += data->meshes[meshIdx].primitives_count;
        }

        std::vector<Primitive> primitives;
        primitives.reserve(primitiveCount);

        for(cgltf_size meshIdx = 0; meshIdx < data->meshes_count; ++meshIdx)
        {
            const cgltf_mesh& mesh = data->meshes[meshIdx];

            const std::string meshName = mesh.name ? mesh.name : "<unnamed mesh>";
            LogScope meshScope("mesh {}/{}", meshName, meshIdx);

            for(cgltf_size primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
            {
                Log::Debug("Loading primitive : {}", primitiveIdx);

                LogScope primitiveScope("prim {}", primitiveIdx);

                const cgltf_primitive& primitive = mesh.primitives[primitiveIdx];

                LoadPrimitive(primitive, bufferData, primitives.emplace_back());
            }
        }

        return true;
    }
};

class Wgpu final
{
public:

    struct Texture
    {
        wgpu::Texture Handle;
        wgpu::TextureView View;
        wgpu::TextureFormat Format;
        unsigned Width{ 0 };
        unsigned Height{ 0 };
        unsigned RowStride{ 0 };
    };

    struct Context
    {
        SDL_Window* Window{ nullptr };
        wgpu::Instance Instance;
        wgpu::Adapter Adapter;
        wgpu::Device Device;
        wgpu::Surface Surface;
        wgpu::TextureFormat SurfaceFormat;
    };

    static inline uint8_t s_ContextBuf[sizeof(Context)]{};
    static inline Context* s_Context{nullptr};

    static Result2<wgpu::Instance> CreateInstance()
    {
        static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor instanceDesc //
            {
                .requiredFeatureCount = 1,
                .requiredFeatures = &kTimedWaitAny,
            };
        wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);

        MLG_CHECK(instance, "Failed to create WGPUInstance");

        return instance;
    }

    static Result2<wgpu::Adapter> CreateAdapter(wgpu::Instance instance)
    {
        static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;

        wgpu::Adapter adapter;

        auto rqstAdapterCb = [&adapter](wgpu::RequestAdapterStatus status,
                                wgpu::Adapter receivedAdapter,
                                wgpu::StringView message) -> void
        {
            if(status != wgpu::RequestAdapterStatus::Success)
            {
                Log::Error("RequestAdapter failed: {}", std::string(message.data, message.length));
            }
            else
            {
                adapter = std::move(receivedAdapter);
            }
        };

        wgpu::RequestAdapterOptions options //
            {
                .nextInChain = nullptr,
                .powerPreference = wgpu::PowerPreference::HighPerformance,
                .forceFallbackAdapter = false,
#if defined(_WIN32)
                .backendType = wgpu::BackendType::Vulkan,
                //.backendType = wgpu::BackendType::D3D12,
#elif defined(__EMSCRIPTEN__)
                .backendType = wgpu::BackendType::WebGPU,
#else
                .backendType = wgpu::BackendType::Vulkan,
#endif
                .compatibleSurface = nullptr,
            };

        wgpu::Future fut =
            instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly, rqstAdapterCb);

        wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

        MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
            "Failed to create WGPUAdapter - WaitAny failed");

        const bool sunpportsIndirectFirstInstance =
            adapter.HasFeature(wgpu::FeatureName::IndirectFirstInstance);

        MLG_CHECK(sunpportsIndirectFirstInstance,
            "IndirectFirstInstance feature is not supported");

        return adapter;
    }

    static Result2<wgpu::Device> CreateDevice(wgpu::Instance instance, wgpu::Adapter adapter)
    {
        // TODO(KB) - handle device lost.
        auto deviceLostCb = [](const wgpu::Device& device [[maybe_unused]],
                                wgpu::DeviceLostReason reason,
                                wgpu::StringView message)
        {
            Log::Error("Device lost (reason:{}): {}",
                static_cast<int>(reason),
                std::string(message.data, message.length));

            // CallbackCancelled indicates intentional device destruction.
            // exit() was probably already called, or program terminated normally.
            if(wgpu::DeviceLostReason::CallbackCancelled != reason)
            {
                exit(0);
            }
        };

        auto uncapturedErrorCb = [](const wgpu::Device& device [[maybe_unused]],
                                    wgpu::ErrorType errorType,
                                    wgpu::StringView message)
        {
            Log::Error("Uncaptured error (type:{}): {}",
                static_cast<int>(errorType),
                std::string(message.data, message.length));
        };

        wgpu::FeatureName requiredFeatures[] = //
            {
                wgpu::FeatureName::IndirectFirstInstance,
            };

        wgpu::DeviceDescriptor deviceDesc //
            {
                {
                    .label = "MainDevice",
                    .requiredFeatureCount = std::size(requiredFeatures),
                    .requiredFeatures = requiredFeatures,
                },
            };
        deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, deviceLostCb);
        deviceDesc.SetUncapturedErrorCallback(uncapturedErrorCb);

        wgpu::Device device;

        auto rqstDeviceCb = [&device](wgpu::RequestDeviceStatus status,
                                wgpu::Device receivedDevice,
                                wgpu::StringView message) -> void
        {
            if(status != wgpu::RequestDeviceStatus::Success)
            {
                Log::Error("RequestDevice failed: {}", std::string(message.data, message.length));
            }
            else
            {
                device = std::move(receivedDevice);
            }
        };

        wgpu::Future fut =
            adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly, rqstDeviceCb);

        wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

        MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
            "Failed to create WGPUDevice - WaitAny failed");

        return device;
    }

    #if defined(__EMSCRIPTEN__)

    static Result2<wgpu::Surface>
    CreateWgpuSurface(wgpu::Instance instance, SDL_Window* window)
    {
        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
        canvas_desc.selector = "#canvas";

        wgpu::SurfaceDescriptor surface_desc = {};
        surface_desc.nextInChain = &canvas_desc;
        wgpu::Surface surface = instance.CreateSurface(&surface_desc);

        MLG_CHECK(surface, "Failed to create WGPUSurface from SDL window");

        return surface;
    }

    #else

    static Result2<wgpu::Surface> CreateSurface(wgpu::Instance instance, SDL_Window* window)
    {
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

        WGPUSurfaceDescriptor surface_descriptor = {};
        WGPUSurface rawSurface = {};

        WGPUSurfaceSourceWindowsHWND surface_src_hwnd = {};
        surface_src_hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        surface_src_hwnd.hinstance = ::GetModuleHandle(NULL);
        surface_src_hwnd.hwnd = hwnd;
        surface_descriptor.nextInChain = &surface_src_hwnd.chain;
        rawSurface = wgpuInstanceCreateSurface(instance.Get(), &surface_descriptor);

        wgpu::Surface surface = wgpu::Surface::Acquire(rawSurface);

        MLG_CHECK(surface, "Failed to create WGPUSurface from SDL window");

        return surface;
    }

    #endif // defined(__EMSCRIPTEN__)

    static wgpu::PresentMode ChoosePresentMode(const wgpu::PresentMode* availableModes, size_t modeCount)
    {
        wgpu::PresentMode presentMode = wgpu::PresentMode::Undefined;
        for(size_t i = 0; i < modeCount; ++i)
        {
            // Prefer mailbox if available
            if(presentMode == wgpu::PresentMode::Undefined || presentMode == wgpu::PresentMode::Fifo)
            {
                switch(availableModes[i])
                {
                    case wgpu::PresentMode::Fifo:
                        presentMode = wgpu::PresentMode::Fifo;
                        break;
                    case wgpu::PresentMode::Mailbox:
                        presentMode = wgpu::PresentMode::Mailbox;
                        break;
                    default:
                        break;
                }
            }
        }

        return presentMode;
    }

    static wgpu::TextureFormat ChooseBackbufferFormat(const wgpu::TextureFormat* availableFormats, size_t formatCount)
    {
        // Prefer BGRA8Unorm if available
        for(size_t i = 0; i < formatCount; ++i)
        {
            if(availableFormats[i] == wgpu::TextureFormat::BGRA8Unorm ||
                availableFormats[i] == wgpu::TextureFormat::RGBA8Unorm)
            {
                return availableFormats[i];
            }
        }
        // Fallback to first available format
        return availableFormats[0];
    }

    static Result2<bool> ConfigureSurface(wgpu::Adapter adapter,
        wgpu::Device device,
        wgpu::Surface surface,
        const uint32_t width,
        const uint32_t height,
        wgpu::TextureFormat& textureFormat)
    {
        wgpu::SurfaceCapabilities capabilities;
        MLG_CHECK(surface.GetCapabilities(adapter, &capabilities),
            "surface.GetCapabilities failed");

        wgpu::PresentMode presentMode =
            ChoosePresentMode(capabilities.presentModes, capabilities.presentModeCount);
        MLG_CHECK(presentMode != wgpu::PresentMode::Undefined,
            "No supported present mode found");

        wgpu::TextureFormat format =
            ChooseBackbufferFormat(capabilities.formats, capabilities.formatCount);
        MLG_CHECK(format != wgpu::TextureFormat::Undefined,
            "No supported backbuffer format found");

        wgpu::SurfaceConfiguration config //
            {
                .device = device,
                .format = format,
                .usage = wgpu::TextureUsage::RenderAttachment,
                .width = width,
                .height = height,
                .alphaMode = wgpu::CompositeAlphaMode::Opaque,
                .presentMode = presentMode,
            };

        surface.Configure(&config);

        textureFormat = format;

        return true;
    }

    static bool CreateTexture(Context* ctx,
        const unsigned width,
        const unsigned height,
        const std::string& name,
        Texture& outTexture)
    {
        const uint32_t rowPitch = width * 4;
        const uint32_t alignedRowPitch = (rowPitch + 255) & ~255;

        wgpu::TextureDescriptor textureDesc //
            {
                .label = name.c_str(),
                .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = width,
                    .height = height,
                    .depthOrArrayLayers = 1,
                },
                .format = kTextureFormat,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        wgpu::Texture texture = ctx->Device.CreateTexture(&textureDesc);
        MLG_CHECK(texture, "Failed to create texture");

        wgpu::TextureView texView = texture.CreateView();
        MLG_CHECK(texView, "Failed to create texture view for texture");


        outTexture = Texture//
        {
            .Handle = texture,
            .View = texView,
            .Width = width,
            .Height = height,
            .RowStride = alignedRowPitch
        };

        return true;
    }

    static Result2<Context*> Startup()
    {
        MLG_CHECK(!s_Context, "WGPU already started");

        MLG_CHECK(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        SDL_Rect displayRect;
        MLG_CHECK(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect),
            SDL_GetError());
        const int winW = displayRect.w * 3 / 4; // 0.75
        const int winH = displayRect.h * 3 / 4;//0.75

        auto window = SDL_CreateWindow(APP_NAME, winW, winH, SDL_WINDOW_RESIZABLE);
        MLG_CHECK(window, SDL_GetError());

        auto cleanup = Defer([&](){SDL_DestroyWindow(window);});

        auto instance = CreateInstance();
        MLG_CHECK(instance);

        auto adapter = CreateAdapter(*instance);
        MLG_CHECK(adapter);

        auto device = CreateDevice(*instance, *adapter);
        MLG_CHECK(device);

        auto surface = CreateSurface(*instance, window);
        MLG_CHECK(surface);

        wgpu::TextureFormat surfaceFormat;
        MLG_CHECK(ConfigureSurface(*adapter, *device, *surface, winW, winH, surfaceFormat));

        s_Context = ::new(s_ContextBuf) Context//
        {
            .Window = window,
            .Instance = *instance,
            .Adapter = *adapter,
            .Device = *device,
            .Surface = *surface,
            .SurfaceFormat = surfaceFormat,
        };

        cleanup.Cancel();

        return s_Context;
    }

    static void Shutdown()
    {
        if(!s_Context)
        {
            return;
        }

        SDL_DestroyWindow(s_Context->Window);
        SDL_Quit();

        ::memset(s_ContextBuf, 0xFE, sizeof(s_ContextBuf));
        s_Context = nullptr;
    }
};

Result2<Wgpu::Context*> Startup()
{
    FileIo::Startup();
    return Wgpu::Startup();
}

Result2<bool> Shutdown()
{
    Wgpu::Shutdown();
    FileIo::Shutdown();
    return true;
}

}   // namespace mlg

mlg::Result2<bool> MainLoop()
{
    static constexpr const char* SCENE1_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
    static constexpr const char* SCENE2_PATH = "C:/Users/kbaca/Downloads/HiddenAlley2/ph_hidden_alley.gltf";

    MLG_CHECK(mlg::Startup());

    auto cleanup = mlg::Defer([]{ mlg::Shutdown(); });

    MLG_CHECK(mlg::Gltf::Load(SCENE2_PATH));

    cleanup.Cancel();

    mlg::Shutdown();

    return true;
}

int main(int, char* /*argv[]*/)
{
    if(!MainLoop())
    {
        return -1;
    }
    return 0;
}