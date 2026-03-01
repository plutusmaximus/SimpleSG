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

#include <filesystem>
#include <format>
#include <iostream>
#include <ranges>
#include <stack>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

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

struct ResultFail final {};

struct ResultSuccess final {};

template<typename T = ResultSuccess>
class Result2 : private std::variant<ResultFail, T>
{
public:

    using Base = std::variant<ResultFail, T>;

    using Base::Base;

    operator bool() const
    {
        return std::holds_alternative<T>(*this);
    }

    T& operator*()
    {
        return std::get<T>(*this);
    }

    const T& operator*() const
    {
        return std::get<T>(*this);
    }

    T* operator->()
    {
        return &std::get<T>(*this);
    }

    const T* operator->() const
    {
        return &std::get<T>(*this);
    }
};

using Index = uint32_t;

struct Position
{
    float X, Y, Z;
};

struct Normal
{
    float X, Y, Z;
};

struct TexCoord
{
    float U, V;
};

struct Color
{
    float R, G, B, A;
};

struct Vertex
{
    Position Pos;
    Normal Norm;
    TexCoord UV;
};

struct BufferRange
{
    size_t ByteOffset;
    size_t ByteCount;
    size_t ItemCount;
    std::string BufferUri;
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
                s_LogPrefix += " : ";
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
    } while (0);

class FileFetcher
{
public:

    enum RequestStatus
    {
        None,
        Failure,
        Pending,
        Success,
    };

    class Request
    {
    public:

        explicit Request(const std::string& filePath)
            : FilePath(filePath)
        {
        }

        ~Request()
        {
            if(IsPending())
            {
                SetComplete(Failure);
            }
        }

        bool IsPending() const { return m_Status == Pending; }
        bool Succeeded() const { return m_Status == Success; }

        std::string FilePath;
        std::vector<uint8_t> Data;
        size_t BytesRequested{0};
        size_t BytesRead{0};

    private:

        friend class FileFetcher;

        void SetComplete(RequestStatus status)
        {
            if(!IsPending())
            {
                return;
            }

            if(m_hFile)
            {
                ::CancelIoEx(m_hFile, &m_Ov);
                ::CloseHandle(m_hFile);
                m_hFile = nullptr;
            }

            m_Status = status;
        }

        HANDLE m_hFile{nullptr};
        OVERLAPPED m_Ov{0};
        RequestStatus m_Status{None};
    };

    static Result2<ResultSuccess> Fetch(Request& request)
    {
        request.m_hFile = ::CreateFileA(request.FilePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);

        MLG_CHECK(request.m_hFile != INVALID_HANDLE_VALUE, "Failed to open file: {}", request.FilePath);

        if(request.BytesRequested == 0)
        {
            auto result = GetFileSize(request);
            MLG_CHECK(result, "Failed to get file size: {}", request.FilePath);
            request.BytesRequested = *result;
        }

        if(request.Data.size() < request.BytesRequested)
        {
            request.Data.resize(request.BytesRequested);
        }

        const ULONG_PTR completionKey = reinterpret_cast<ULONG_PTR>(&request);
        MLG_CHECK(nullptr != ::CreateIoCompletionPort(request.m_hFile, s_IOCP, completionKey, 0),
            "Failed to bind file to IOCP: {}, error: {}",
            request.FilePath,
            ::GetLastError());

        MLG_CHECK(IssueRead(request));

        request.m_Status = Pending;

        return ResultSuccess{};
    }

    static Result2<ResultSuccess> ProcessCompletions()
    {
        BOOL ok;
        std::array<OVERLAPPED_ENTRY, 8> entries = {};
        ULONG numEntriesRemoved = 0;
        {
            ok = ::GetQueuedCompletionStatusEx(s_IOCP,
                entries.data(),
                static_cast<ULONG>(entries.size()),
                &numEntriesRemoved,
                0,
                FALSE);
        }

        if(!ok)
        {
            const DWORD err = ::GetLastError();

            if(WAIT_TIMEOUT == err)
            {
                // No completions available
                return ResultSuccess{};
            }

            if(ERROR_ABANDONED_WAIT_0 == err)
            {
                // IOCP was closed during shutdown.
                return ResultSuccess{};
            }

            // Some other error occurred - assume it's fatal.
            Log::Error("GetQueuedCompletionStatusEx failed, error: {}", err);

            return {};
        }

        // If we get here, at least one read completed successfully.

        for(ULONG i = 0; i < numEntriesRemoved; ++i)
        {
            OVERLAPPED_ENTRY& entry = entries[i];
            Request* req = reinterpret_cast<Request*>(entry.lpCompletionKey);

            if(!req)
            {
                continue;
            }

            req->BytesRead += entry.dwNumberOfBytesTransferred;

            // Attempt to read more bytes.  This could complete immediately.
            IssueRead(*req);
        }

        return ResultSuccess{};
    }

private:

    static Result2<size_t> GetFileSize(const Request& request)
    {
        static_assert(sizeof(size_t) >= sizeof(LARGE_INTEGER::QuadPart), "size_t is too small to hold file size");
        LARGE_INTEGER size;
        MLG_CHECK(GetFileSizeEx(request.m_hFile, &size),
            "Failed to open file: {}, error: {}",
            request.m_hFile,
            ::GetLastError());

        return static_cast<size_t>(size.QuadPart);
    }

    static Result2<ResultSuccess> IssueRead(Request& req)
    {
        bool done = false;
        while(req.BytesRead < req.BytesRequested && !done)
        {
            LARGE_INTEGER li;
            li.QuadPart = req.BytesRead;

            // Set up the offset into the file from which to read.
            req.m_Ov.Offset = li.LowPart;
            req.m_Ov.OffsetHigh = li.HighPart;

            const size_t bytesRemaining = req.BytesRequested - req.BytesRead;

            DWORD bytesRead = 0;

            // Re-issue read for remaining bytes
            const BOOL ok = ::ReadFile(req.m_hFile,
                req.Data.data() + req.BytesRead,
                static_cast<DWORD>(bytesRemaining),
                &bytesRead,
                &req.m_Ov);

            if(ok)
            {
                // Request completed synchronously - loop again if necessary
                req.BytesRead += bytesRead;
                continue;
            }

            const DWORD err = ::GetLastError();

            if(err == ERROR_IO_PENDING)
            {
                // Still pending, break out of the loop.
                done = true;
                continue;
            }

            Log::Error("Failed to issue read for file: {}, error: {}", req.FilePath, err);

            req.SetComplete(Failure);

            return {};
        }

        if(req.IsPending() && req.BytesRead >= req.BytesRequested)
        {
            req.SetComplete(Success);
        }

        return ResultSuccess{};
    }

    static inline HANDLE s_IOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
};

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
        BufferRange IndexRange;
        BufferRange PositionRange;
        BufferRange NormalRange;
        BufferRange TexCoordRange[kMaxTexCoords];
        Material Mtl;
    };

    explicit Gltf(const std::filesystem::path& gltfFilePath)
        : m_GltfFetchRequest(gltfFilePath.string())
    {
    }

    Result2<ResultSuccess> Load()
    {
        MLG_CHECK(None == m_CurState, "Gltf is already loading or has been loaded");
        m_CurState = Begin;
        return ResultSuccess{};
    }

    bool IsPending() const { return m_CurState != Success && m_CurState != Failure; }

    bool Succeeded() const { return m_CurState == Success; }

    void Update()
    {
        switch(m_CurState)
        {
            case None:
                Log::Error("Gltf is in None state - cannot update");
                break;

            case Begin:
                if(FileFetcher::Fetch(m_GltfFetchRequest))
                {
                    m_CurState = LoadingGltfFile;
                }
                else
                {
                    m_CurState = Failure;
                }
                break;
            case LoadingGltfFile:
                if(m_GltfFetchRequest.IsPending())
                {
                    return;
                }
                else if(!m_GltfFetchRequest.Succeeded())
                {
                    m_CurState = Failure;
                }
                else
                {
                    cgltf_options options{};
                    cgltf_data* data = NULL;
                    cgltf_result parseResult =
                        cgltf_parse(&options, m_GltfFetchRequest.Data.data(), m_GltfFetchRequest.Data.size(), &data);
                    if(parseResult != cgltf_result_success)
                    {
                        m_CurState = Failure;
                    }
                    else
                    {
                        auto result = LoadScenes(data);
                        if(!result)
                        {
                            m_CurState = Failure;
                        }
                        else
                        {
                            m_CurState = Success;
                        }
                    }
                }
                break;
            case Success:
                break;
            case Failure:
                break;
        }
    }

    std::span<const Primitive> GetPrimitives() const { return m_Primitives; }

private:

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

    Result2<BufferRange>
    GetAccessorRange(const cgltf_accessor& accessor)
    {
        auto logScope = LogScope("accessor {}", accessor.name ? accessor.name : "<unnamed accessor>");

        MLG_CHECK(!accessor.is_sparse, "Sparse accessors are unsupported");

        const cgltf_buffer_view& bufferView = *accessor.buffer_view;

        MLG_CHECK(bufferView.buffer->uri, "Buffer does not have a URI");

        return BufferRange //
            {
                .ByteOffset = bufferView.offset + accessor.offset,
                .ByteCount = accessor.count * cgltf_num_components(accessor.type) *
                             cgltf_component_size(accessor.component_type),
                .ItemCount = accessor.count,
                .BufferUri = bufferView.buffer->uri,
            };
    }

    Result2<std::string> GetTextureUri(const cgltf_texture_view& textureView)
    {
        MLG_CHECK(textureView.texture, "Texture view does not have a texture");
        const cgltf_texture& texture = *textureView.texture;
        MLG_CHECK(texture.image, "Texture image is not set");
        MLG_CHECK(texture.image->uri, "Texture image URI is not set");
        std::string uri = texture.image->uri;

        MLG_CHECK(!uri.empty(), "Texture URI is empty");

        return uri;
    }

    Result2<Material> LoadMaterial(const cgltf_material& material)
    {
        LogScope attributeScope("mtrl {}", material.name ? material.name : "<unnamed material>");

        Material outMaterial;
        outMaterial.Name = material.name ? material.name : "<unnamed material>";
        outMaterial.DoubleSided = material.double_sided;

        MLG_CHECK(material.has_pbr_metallic_roughness,
            "Material does not have PBR metallic-roughness");

        const cgltf_texture_view& baseTexture = material.pbr_metallic_roughness.base_color_texture;
        auto uriResult = GetTextureUri(baseTexture);
        MLG_CHECK(uriResult);
        outMaterial.BaseTextureUri = *uriResult;

        const cgltf_texture_view& metallicRoughnessTexture = material.pbr_metallic_roughness.metallic_roughness_texture;
        auto metallicRoughnessUriResult = GetTextureUri(metallicRoughnessTexture);
        MLG_CHECK(metallicRoughnessUriResult);
        outMaterial.MetallicRoughnessTextureUri = *metallicRoughnessUriResult;

        outMaterial.MetallicFactor = material.pbr_metallic_roughness.metallic_factor;
        outMaterial.RoughnessFactor = material.pbr_metallic_roughness.roughness_factor;

        const cgltf_float (&baseColorFactor)[4] = material.pbr_metallic_roughness.base_color_factor;

        outMaterial.BaseColor.R = baseColorFactor[0];
        outMaterial.BaseColor.G = baseColorFactor[1];
        outMaterial.BaseColor.B = baseColorFactor[2];
        outMaterial.BaseColor.A = baseColorFactor[3];

        return outMaterial;
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

    Result2<Primitive> LoadPrimitive(const cgltf_primitive& primitive)
    {
        MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
            "Only triangle primitives are supported");

        MLG_CHECK(primitive.material, "Primitive does not have a material");

        MLG_CHECK(primitive.attributes_count > 0,
            "Primitive does not have any attributes");

        MLG_CHECK(primitive.targets_count == 0, "Morph targets are not supported");

        MLG_CHECK(!primitive.has_draco_mesh_compression,
            "Draco mesh compression is not supported");

        MLG_CHECK(primitive.indices, "Primitive does not have indices");

        Primitive outPrim;

        auto material = LoadMaterial(*primitive.material);
        MLG_CHECK(material);
        outPrim.Mtl = std::move(*material);

        auto indexRange = GetAccessorRange(*primitive.indices);
        MLG_CHECK(indexRange);

        outPrim.IndexRange = *indexRange;

        const std::span<const cgltf_attribute> attributes(primitive.attributes, primitive.attributes_count);

        for(const auto& attribute : attributes)
        {
            LogScope attributeScope("attr {}", attribute.name ? attribute.name : "<unnamed attribute>");
            const cgltf_accessor& accessor = *attribute.data;

            auto range = GetAccessorRange(accessor);
            MLG_CHECK(range);

            switch(attribute.type)
            {
                case cgltf_attribute_type_position:
                    outPrim.PositionRange = *range;
                    break;

                case cgltf_attribute_type_normal:
                    outPrim.NormalRange = *range;
                    break;

                case cgltf_attribute_type_texcoord:
                    MLG_CHECK(attribute.index < kMaxTexCoords,
                        "Texture coordinate index {} exceeds maximum supported {}",
                        attribute.index,
                        kMaxTexCoords);

                    outPrim.TexCoordRange[attribute.index] = *range;
                    break;

                default:
                    Log::Error("Unsupported attribute type \"{}\"/{}",
                        cgltf_attribute_type_to_string(attribute.type),
                        std::to_underlying(attribute.type));
                    break;
            }
        }

        return outPrim;
    }

    Result2<ResultSuccess> LoadScenes(const cgltf_data* gltf)
    {
        std::span<const cgltf_scene> scenes(gltf->scenes, gltf->scenes_count);

        for(const auto& scene : scenes)
        {
            LogScope sceneScope("scene {}", scene.name ? scene.name : "<unnamed scene>");

            std::span<cgltf_node*> nodes(scene.nodes, scene.nodes_count);

            for(const auto& node : nodes)
            {
                LogScope nodeScope("node {}", node->name ? node->name : "<unnamed node>");
            }
        }

        std::unordered_map<std::string, const cgltf_mesh*> meshes;
        meshes.reserve(gltf->meshes_count);

        m_Materials.reserve(gltf->materials_count);

        m_TexturePaths.reserve(gltf->textures_count);

        size_t primCount = 0;
        for(cgltf_size meshIdx = 0; meshIdx < gltf->meshes_count; ++meshIdx)
        {
            const cgltf_mesh& mesh = gltf->meshes[meshIdx];
            meshes.emplace(mesh.name ? mesh.name : "<unnamed mesh>:" + std::to_string(meshIdx), &mesh);
            primCount += mesh.primitives_count;
        }

        m_Primitives.reserve(primCount);

        for(const auto& [meshName, mesh] : meshes)
        {
            for(cgltf_size primitiveIdx = 0; primitiveIdx < mesh->primitives_count; ++primitiveIdx)
            {
                const cgltf_primitive& primitive = mesh->primitives[primitiveIdx];
                LogScope primitiveScope("prim {}:{}", meshName, primitiveIdx);

                auto prim = LoadPrimitive(primitive);
                if(!prim)
                {
                    Log::Error("Failed to load primitive");
                    continue;
                }

                m_Primitives.emplace_back(std::move(*prim));
            }
        }

        return ResultSuccess{};
    }

    enum State
    {
        None,
        Begin,
        LoadingGltfFile,
        Success,
        Failure
    };

    State m_CurState{None};

    FileFetcher::Request m_GltfFetchRequest;

    std::vector<Primitive> m_Primitives;
    std::unordered_map<std::string, Material> m_Materials;
    std::vector<std::string> m_TexturePaths;
};

class Wgpu final
{
public:

    struct Texture
    {
        wgpu::Texture Handle;
        wgpu::TextureView View;
        wgpu::Buffer StagingBuffer;

        // webgpu texture rows must be 256-byte aligned
        unsigned GetRowStride() const { return ((Handle.GetWidth() * 4) + 255) & ~255; }
    };

    struct Buffer
    {
        wgpu::Buffer Handle;
        wgpu::TextureView View;
        wgpu::TextureFormat Format;
        wgpu::Buffer StagingBuffer;
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

    static Result2<wgpu::TextureFormat> ConfigureSurface(wgpu::Adapter adapter,
        wgpu::Device device,
        wgpu::Surface surface,
        const uint32_t width,
        const uint32_t height)
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

        return format;
    }

    static Result2<Texture> CreateTexture(Context* ctx,
        const unsigned width,
        const unsigned height,
        const std::string& name)
    {
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

        return Texture//
        {
            .Handle = texture,
            .View = texView
        };
    }

    static Result2<wgpu::Buffer> CreateVertexBuffer(
        Context* ctx, const size_t vertexCount, const std::string& name)
    {
        wgpu::BufferDescriptor bufferDesc //
            {
                .label = name.c_str(),
                .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
                .size = vertexCount * sizeof(Vertex),
            };

        wgpu::Buffer buffer = ctx->Device.CreateBuffer(&bufferDesc);
        MLG_CHECK(buffer, "Failed to create vertex buffer");

        return buffer;
    }

    static Result2<wgpu::Buffer> CreateIndexBuffer(
        Context* ctx, const size_t indexCount, const std::string& name)
    {
        wgpu::BufferDescriptor bufferDesc //
            {
                .label = name.c_str(),
                .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
                .size = indexCount * sizeof(uint32_t),
            };

        wgpu::Buffer buffer = ctx->Device.CreateBuffer(&bufferDesc);
        MLG_CHECK(buffer, "Failed to create index buffer");

        return buffer;
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

        auto surfaceFormat = ConfigureSurface(*adapter, *device, *surface, winW, winH);
        MLG_CHECK(surfaceFormat);

        s_Context = ::new(s_ContextBuf) Context//
        {
            .Window = window,
            .Instance = *instance,
            .Adapter = *adapter,
            .Device = *device,
            .Surface = *surface,
            .SurfaceFormat = *surfaceFormat,
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

class ResourceLoader final
{
public:

    explicit ResourceLoader(const std::span<const mlg::Gltf::Primitive>& primitives)
        : m_Primitives(primitives)
    {
    }

    bool IsPending() const { return m_CurState != Success && m_CurState != Failure; }

    bool Succeeded() const { return m_CurState == Success; }

    Result2<ResultSuccess> Load()
    {
        MLG_CHECK(None == m_CurState, "Resources are already loading or have been loaded");
        m_CurState = Begin;
        return ResultSuccess{};
    }

    void Update()
    {
        switch(m_CurState)
        {
            case None:
                Log::Error("ResourceLoader is in None state - cannot update");
                break;
            case Begin:

                {
                    std::unordered_set<std::string> bufferUris;

                    for(const auto& prim : m_Primitives)
                    {
                        bufferUris.insert(prim.IndexRange.BufferUri);
                    }

                    for(const auto& uri : bufferUris)
                    {
                        m_Requests.emplace_back(uri);
                        if(!FileFetcher::Fetch(m_Requests.back()))
                        {
                            m_CurState = Failure;
                        }
                        else
                        {
                            m_CurState = LoadingBuffers;
                        }
                    }
                }
                break;
            case LoadingBuffers:
                for(auto& request : m_Requests)
                {
                    if(request.IsPending())
                    {
                        return;
                    }
                }
                m_CurState = LoadingVertices;
                m_CurState = Success;
                break;
            case LoadingVertices:
                // Load vertices
                m_CurState = LoadingIndices;
                break;
            case LoadingIndices:
                // Load indices
                m_CurState = LoadingTextures;
                break;
            case LoadingTextures:
                // Load textures
                m_CurState = Success;
                break;
            case Success:
                break;
            case Failure:
                break;
        }
    }

private:

    enum State
    {
        None,
        Begin,
        LoadingBuffers,
        LoadingVertices,
        LoadingIndices,
        LoadingTextures,
        Success,
        Failure
    };

    std::span<const mlg::Gltf::Primitive> m_Primitives;

    std::vector<FileFetcher::Request> m_Requests;

    State m_CurState{None};
};

Result2<Wgpu::Context*> Startup()
{
    return Wgpu::Startup();
}

Result2<mlg::ResultSuccess> Shutdown()
{
    Wgpu::Shutdown();
    return mlg::ResultSuccess{};
}

}   // namespace mlg

mlg::Result2<mlg::ResultSuccess> MainLoop()
{
    static constexpr const char* SCENE1_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
    static constexpr const char* SCENE2_PATH = "C:/Users/kbaca/Downloads/HiddenAlley2/ph_hidden_alley.gltf";

    auto wgpuCtx = mlg::Startup();
    MLG_CHECK(wgpuCtx);

    auto cleanup = mlg::Defer([]{ mlg::Shutdown(); });

    mlg::Gltf gltf{SCENE2_PATH};
    gltf.Load();

    while(gltf.IsPending())
    {
        gltf.Update();
        MLG_CHECK(mlg::FileFetcher::ProcessCompletions());
    }

    const auto& primitives = gltf.GetPrimitives();

    /*mlg::ResourceLoader loader{primitives};
    loader.Load();

    while(loader.IsPending())
    {
        loader.Update();
        MLG_CHECK(mlg::FileFetcher::ProcessCompletions());
    }*/

    std::vector<const mlg::Gltf::Primitive*> byIndexOffset;
    byIndexOffset.reserve(primitives.size());

    for(const auto& prim : primitives)
    {
        byIndexOffset.push_back(&prim);
    }

    std::vector<const mlg::Gltf::Primitive*> byPositionOffset = byIndexOffset;
    std::vector<const mlg::Gltf::Primitive*> byNormalOffset = byIndexOffset;

    std::ranges::sort(byIndexOffset,
        [](const mlg::Gltf::Primitive* a, const mlg::Gltf::Primitive* b)
        { return a->IndexRange.ByteOffset < b->IndexRange.ByteOffset; });

    std::ranges::sort(byPositionOffset,
        [](const mlg::Gltf::Primitive* a, const mlg::Gltf::Primitive* b)
        { return a->PositionRange.ByteOffset < b->PositionRange.ByteOffset; });

    std::ranges::sort(byNormalOffset,
        [](const mlg::Gltf::Primitive* a, const mlg::Gltf::Primitive* b)
        { return a->NormalRange.ByteOffset < b->NormalRange.ByteOffset; });

    size_t vertexCount = 0;
    for(const auto& prim : primitives)
    {
        vertexCount += prim.PositionRange.ItemCount;
    }

    auto vb = mlg::Wgpu::CreateVertexBuffer(*wgpuCtx, vertexCount, "VertexBuffer");
    MLG_CHECK(vb);

    //void* vbData = vb->GetMappedRange();

    size_t indexCount = 0;
    for(const auto& prim : primitives)
    {
        indexCount += prim.IndexRange.ItemCount;
    }

    auto ib = mlg::Wgpu::CreateIndexBuffer(*wgpuCtx, indexCount, "IndexBuffer");
    MLG_CHECK(ib);

    MLG_CHECK(gltf.Succeeded());

    cleanup.Cancel();

    mlg::Shutdown();

    return mlg::ResultSuccess{};
}

int main(int, char* /*argv[]*/)
{
    if(!MainLoop())
    {
        return -1;
    }
    return 0;
}