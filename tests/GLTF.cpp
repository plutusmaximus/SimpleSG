#define _CRT_SECURE_NO_WARNINGS

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "FileIo.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <stack>
#include <thread>
#include <type_traits>
#include <unordered_map>

static constexpr int MAX_TEX_COORDS = 6;

namespace
{
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

    struct Material
    {
        std::string BaseTextureUri;
        std::string MetallicRoughnessTextureUri;
        float BaseColor[4];
        float MetallicFactor;
        float RoughnessFactor;
    };

    struct Primitive
    {
        std::vector<uint32_t> Indices;
        std::span<const Position> Positions;
        std::span<const Normal> Normals;
        std::span<const TexCoord> TexCoords[MAX_TEX_COORDS];

        Material Mtl;
    };
} // namespace

static std::vector<std::string> s_ErrorPrefixStack;
static std::string s_ErrorPrefix;

static void MakeErrorPrefix()
{
    s_ErrorPrefix.clear();
    for(const auto& prefix : s_ErrorPrefixStack)
    {
        if(!s_ErrorPrefix.empty())
        {
            s_ErrorPrefix += ": ";
        }
        s_ErrorPrefix += prefix;
    }
}

template<typename... Args>
static void PushErrorPrefix(std::format_string<Args...> fmt, Args&&... args)
{
    s_ErrorPrefixStack.push_back(std::format(fmt, std::forward<Args>(args)...));
    MakeErrorPrefix();
}

static void PopErrorPrefix()
{
    if(!s_ErrorPrefixStack.empty())
    {
        s_ErrorPrefixStack.pop_back();
        MakeErrorPrefix();
    }
}

namespace
{
struct ErrorScope
{
    template<typename... Args>
    ErrorScope(std::format_string<Args...> fmt, Args&&... args)
    {
        PushErrorPrefix(fmt, std::forward<Args>(args)...);
    }

    ~ErrorScope()
    {
        PopErrorPrefix();
    }
};
}

template<typename... Args>
static inline void LOG_ERROR(std::format_string<Args...> fmt, Args&&... args)
{
    if(!s_ErrorPrefix.empty())
    {
        std::cerr << s_ErrorPrefix << ": ";
    }
    std::cerr << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

static inline void LOG_ERROR(std::string_view msg)
{
    if(!s_ErrorPrefix.empty())
    {
        std::cerr << s_ErrorPrefix << ": ";
    }
    std::cerr << msg << std::endl;
}

static inline void LOG_ERROR() {}

#define MLG_CHECK(expr, stmt, ...) \
    do { \
        if (!(expr)) { \
            LOG_ERROR(__VA_ARGS__); \
            stmt; \
        } \
    } while (0)

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

template <>
struct std::formatter<cgltf_result> : std::formatter<std::string_view>
{
    auto format(cgltf_result r, format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(
            cgltf_result_to_string(r), ctx);
    }
};

static bool LoadGLTF(const std::filesystem::path& gltfFilePath, cgltf_data*& outData)
{
    auto fetchResult = FileIo::Fetch(gltfFilePath.string());
    MLG_CHECK(fetchResult,
        return false,
        "Failed to fetch glTF file {}: {}",
        gltfFilePath.string(),
        fetchResult.error());
    auto fetchToken = *fetchResult;

    while(FileIo::IsPending(fetchToken))
    {
        std::this_thread::yield();
    }

    auto resultData = FileIo::GetResult(fetchToken);
    MLG_CHECK(resultData,
        return false,
        "Failed to get glTF file {}: {}",
        gltfFilePath.string(),
        resultData.error());

    cgltf_options options{};
    cgltf_data* data = NULL;
    cgltf_result parseResult = cgltf_parse(&options, resultData->data(), resultData->size(), &data);
    MLG_CHECK(parseResult == cgltf_result_success,
        return false,
        "Failed to parse glTF file: {}",
        parseResult);

    outData = data;
    return true;
}

static bool LoadBuffers(const cgltf_data *data, const std::filesystem::path &parentPath, std::unordered_map<std::string, FileIo::FetchData> &bufferData)
{
    bufferData.clear();
    bufferData.reserve(data->buffers_count);

    std::unordered_map<std::string, FileIo::AsyncToken> asyncTokens;
    asyncTokens.reserve(data->buffers_count);

    for (cgltf_size i = 0; i < data->buffers_count; ++i)
    {
        cgltf_buffer* buffer = &data->buffers[i];
        auto result = FileIo::Fetch((parentPath / buffer->uri).string());

        MLG_CHECK(result, return false, "Failed to fetch buffer {}: {}", buffer->uri, result.error());

        asyncTokens.emplace(buffer->uri, *result);
    }

    for (auto& [uri, token] : asyncTokens)
    {
        if (FileIo::IsPending(token))
        {
            std::this_thread::yield();
        }
    }

    for (auto& [uri, token] : asyncTokens)
    {
        auto result = FileIo::GetResult(token);
        MLG_CHECK(result, return false, "Failed to get buffer {}: {}", uri, result.error());
        bufferData.emplace(uri, std::move(*result));
    }
    return true;
}

static const uint8_t*
GetBufferData(const cgltf_attribute& attribute,
    const std::unordered_map<std::string, FileIo::FetchData>& bufferData)
{
    const cgltf_accessor& accessor = *attribute.data;
    MLG_CHECK(!accessor.is_sparse, return nullptr, "{}:{}:{} is sparse, which is not supported yet",
                         attribute.name ? attribute.name : "<unnamed attribute>",
                         accessor.name ? accessor.name : "<unnamed accessor>",
                         attribute.data->buffer_view->buffer->uri);

    const cgltf_buffer_view& bufferView = *accessor.buffer_view;
    const cgltf_buffer& buffer = *bufferView.buffer;
    auto it = bufferData.find(buffer.uri);
    MLG_CHECK(it != bufferData.end(), return nullptr, "Buffer {} not found", buffer.uri);
    return it->second.data() + bufferView.offset + accessor.offset;
}

static bool LoadTexture(const cgltf_texture& texture, std::string& uri)
{
    uri.clear();

    MLG_CHECK(texture.image, return false, "Texture image is not set");
    MLG_CHECK(texture.image->uri, return false, "Texture image URI is not set");
    uri = texture.image->uri;

    MLG_CHECK(!uri.empty(), return false, "Texture URI is empty");

    return true;
}

static bool LoadBaseTexture(const cgltf_texture_view& textureView, Material& outMaterial)
{
    if(!textureView.texture)
    {
        return true;
    }

    MLG_CHECK(LoadTexture(*textureView.texture, outMaterial.BaseTextureUri), return false);

    return true;
}

static bool LoadMetallicRoughnessTexture(const cgltf_texture_view& textureView, Material& outMaterial)
{
    if(!textureView.texture)
    {
        return true;
    }

    MLG_CHECK(LoadTexture(*textureView.texture, outMaterial.MetallicRoughnessTextureUri), return false);
    return true;
}

static bool LoadMaterial(const cgltf_material& material, Material& outMaterial)
{
    ErrorScope attributeScope("mtrl {}", material.name ? material.name : "<unnamed material>");

    MLG_CHECK(material.has_pbr_metallic_roughness,
        return false,
        "Material does not have PBR metallic-roughness");

    const cgltf_texture_view& baseTexture = material.pbr_metallic_roughness.base_color_texture;

    MLG_CHECK(LoadBaseTexture(baseTexture, outMaterial), return false);

    const cgltf_texture_view& metallicRoughnessTexture = material.pbr_metallic_roughness.metallic_roughness_texture;

    MLG_CHECK(LoadMetallicRoughnessTexture(metallicRoughnessTexture, outMaterial), return false);

    outMaterial.MetallicFactor = material.pbr_metallic_roughness.metallic_factor;
    outMaterial.RoughnessFactor = material.pbr_metallic_roughness.roughness_factor;

    const cgltf_float (&baseColorFactor)[4] = material.pbr_metallic_roughness.base_color_factor;

    std::copy(baseColorFactor, baseColorFactor + 4, outMaterial.BaseColor);

    return true;
}

static bool LoadIndices(const cgltf_primitive& primitive,
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

        MLG_CHECK(!outIndices.empty(), return false, "Failed to generate indices for primitive without indices");

        return true;
    }

    const cgltf_accessor& accessor = *primitive.indices;
    MLG_CHECK(!accessor.is_sparse, return false, "Sparse indices are not supported");

    const cgltf_buffer_view& bufferView = *accessor.buffer_view;
    const cgltf_buffer& buffer = *bufferView.buffer;
    auto it = bufferData.find(buffer.uri);
    MLG_CHECK(it != bufferData.end(), return false, "Buffer {} not found", buffer.uri);

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
                return false,
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
static bool
LoadAttributes(const cgltf_attribute& attribute,
    const std::unordered_map<std::string, FileIo::FetchData>& bufferData,
    std::span<const typename AttributeTraits<AttrType>::type>& outData)
{
    MLG_CHECK(attribute.data->component_type == AttributeTraits<AttrType>::component_type,
        return false,
        "Attribute {} has unsupported component type {}",
        attribute.name ? attribute.name : "<unnamed attribute>",
        std::to_underlying(attribute.data->component_type));

    const uint8_t* bufferDataPtr = GetBufferData(attribute, bufferData);
    MLG_CHECK(bufferDataPtr,
        return false,
        "Failed to get buffer data for attribute {}",
        attribute.name ? attribute.name : "<unnamed attribute>");

    const typename AttributeTraits<AttrType>::type* data =
        reinterpret_cast<const typename AttributeTraits<AttrType>::type*>(bufferDataPtr);

    outData =
        std::span<const typename AttributeTraits<AttrType>::type>(data, attribute.data->count);

    return true;
}

static bool LoadPrimitive(const cgltf_primitive& primitive,
    const std::unordered_map<std::string, FileIo::FetchData>& bufferData,
    Primitive& outAttributes)
{
    MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
        return false,
        "Only triangle primitives are supported");

    MLG_CHECK(primitive.material, return false, "Primitive does not have a material");

    MLG_CHECK(primitive.attributes_count > 0,
        return false,
        "Primitive does not have any attributes");

    MLG_CHECK(primitive.targets_count == 0, return false, "Morph targets are not supported");

    MLG_CHECK(!primitive.has_draco_mesh_compression,
        return false,
        "Draco mesh compression is not supported");

    MLG_CHECK(LoadMaterial(*primitive.material, outAttributes.Mtl), return false);

    MLG_CHECK(LoadIndices(primitive, bufferData, outAttributes.Indices), return false);

    const std::span<const cgltf_attribute> attributes(primitive.attributes, primitive.attributes_count);

    for(cgltf_size attributeIdx = 0; attributeIdx < attributes.size(); ++attributeIdx)
    {
        ErrorScope attributeScope("attr {}", attributeIdx);

        const cgltf_attribute& attribute = attributes[attributeIdx];

        switch(attribute.type)
        {
            case cgltf_attribute_type_position:
                MLG_CHECK(LoadAttributes<cgltf_attribute_type_position>(attribute,
                        bufferData,
                        outAttributes.Positions),
                    return false);
                break;

            case cgltf_attribute_type_normal:
                MLG_CHECK(LoadAttributes<cgltf_attribute_type_normal>(attribute,
                        bufferData,
                        outAttributes.Normals),
                    return false);
                break;

            case cgltf_attribute_type_texcoord:
                MLG_CHECK(attribute.index < MAX_TEX_COORDS,
                    return false,
                    "Texture coordinate index {} exceeds maximum supported {}",
                    attribute.index,
                    MAX_TEX_COORDS);

                MLG_CHECK(LoadAttributes<cgltf_attribute_type_texcoord>(attribute,
                        bufferData,
                        outAttributes.TexCoords[attribute.index]),
                    return false);
                break;

            default:
                LOG_ERROR("Unsupported attribute type {}", std::to_underlying(attribute.type));
                break;
        }
    }

    return true;
}

int main(int, char* /*argv[]*/)
{
    FileIo::Startup();

    const std::filesystem::path gltfFilePath{"C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf"};
    const std::filesystem::path parentPath = gltfFilePath.parent_path();

    cgltf_data* data = nullptr;
    MLG_CHECK(LoadGLTF(gltfFilePath, data), return -1);

    std::unordered_map<std::string, FileIo::FetchData> bufferData;

    MLG_CHECK(LoadBuffers(data, parentPath, bufferData), return -1);

    std::vector<std::string> textureUris;
    textureUris.reserve(data->textures_count);
    for(cgltf_size texIdx = 0; texIdx < data->textures_count; ++texIdx)
    {
        const cgltf_texture& texture = data->textures[texIdx];
        MLG_CHECK(LoadTexture(texture, textureUris.emplace_back()), return -1);
    }

    std::vector<FileIo::AsyncToken> textureFetchTokens;
    textureFetchTokens.reserve(textureUris.size());
    for(const auto& uri : textureUris)
    {
        std::cout << "Fetching texture: " << (parentPath / uri).string() << std::endl;

        auto result = FileIo::Fetch((parentPath / uri).string());
        MLG_CHECK(result, return -1, result.error().GetMessage().c_str());
        textureFetchTokens.emplace_back(std::move(*result));
    }

    for (auto& token : textureFetchTokens)
    {
        if (FileIo::IsPending(token))
        {
            std::this_thread::yield();
        }
    }

    std::vector<Material> materials;
    materials.reserve(data->materials_count);
    for(cgltf_size mtlIdx = 0; mtlIdx < data->materials_count; ++mtlIdx)
    {
        const cgltf_material& material = data->materials[mtlIdx];
        MLG_CHECK(LoadMaterial(material, materials.emplace_back()), return -1);
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
        ErrorScope meshScope("mesh {}", meshIdx);

        const cgltf_mesh& mesh = data->meshes[meshIdx];

        for(cgltf_size primitiveIdx = 0; primitiveIdx < mesh.primitives_count; ++primitiveIdx)
        {
            ErrorScope primitiveScope("prim {}", primitiveIdx);

            const cgltf_primitive& primitive = mesh.primitives[primitiveIdx];

            MLG_CHECK(LoadPrimitive(primitive, bufferData, primitives.emplace_back()), return -1);
        }
    }

    FileIo::Shutdown();

    return 0;
}