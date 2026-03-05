#define _CRT_SECURE_NO_WARNINGS

#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <map>
#include <set>
#include <unordered_set>
#include <variant>
#include <vector>

namespace mlg
{
struct Vec3f{float x, y, z;};

struct Mat44
{
    union
    {
        float m[16];
        float mm[4][4];
        struct
        {
            float m00, m01, m02, m03;
            float m10, m11, m12, m13;
            float m20, m21, m22, m23;
            float m30, m31, m32, m33;
        };
    };
};

Vec3f Normalize(const Vec3f& v)
{
    float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if(length > 0.0f)
    {
        return Vec3f{v.x / length, v.y / length, v.z / length};
    }
    return Vec3f{0.0f, 0.0f, 0.0f};
}

Vec3f Cross(const Vec3f& a, const Vec3f& b)
{
    return Vec3f{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float Dot(const Vec3f& a, const Vec3f& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f Add(const Vec3f& a, const Vec3f& b)
{
    return Vec3f{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f Sub(const Vec3f& a, const Vec3f& b)
{
    return Vec3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

Mat44 Transpose(const Mat44& m)
{
    Mat44 result;
    for(int i = 0; i < 4; ++i)
    {
        for(int j = 0; j < 4; ++j)
        {
            result.mm[i][j] = m.mm[j][i];
        }
    }
    return result;
}

using Position = Vec3f;
using Normal = Vec3f;
struct RgbaColor{float r, g, b, a;};
struct UV {float u, v;};
struct Vertex
{
    Position pos;   //POSITION attribute
    Normal norm;    //NORMAL attribute
    UV uv[2];       //TEXCOORD_0, TEXCOORD_1 attributes
};

struct Mesh
{
    uint32_t NodeIndex;
    uint32_t IndexCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
};

struct Material
{
    const RgbaColor Color;
    const float Metalness;
    const float Roughness;
};

using VertexIndex = uint32_t;

struct ResultFail final {};

struct ResultOk final {};

template<typename T> class ResultBase{};

template<>
class ResultBase<ResultOk>
{
public:
    static constexpr ResultOk Ok;

    static constexpr ResultFail Fail;
};

template<typename SuccessType = ResultOk, typename ErrorType = ResultFail>
class Result final : public ResultBase<SuccessType>, private std::variant<ErrorType, SuccessType>
{
    using Base = std::variant<ErrorType, SuccessType>;

public:

    using Base::Base;

    operator bool() const
    {
        return std::holds_alternative<SuccessType>(*this);
    }

    SuccessType& operator*()
    {
        return std::get<SuccessType>(*this);
    }

    const SuccessType& operator*() const
    {
        return std::get<SuccessType>(*this);
    }

    SuccessType* operator->()
    {
        return &std::get<SuccessType>(*this);
    }

    const SuccessType* operator->() const
    {
        return &std::get<SuccessType>(*this);
    }
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
    static inline void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        LogTo(std::cerr, "[WRN] " + s_LogPrefix, fmt, std::forward<Args>(args)...);
    }

    static inline void Warn(std::string_view msg)
    {
        LogTo(std::cerr, "[WRN] " + s_LogPrefix, msg);
    }

    static inline void Warn() {}

    template<typename... Args>
    static inline void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        LogTo(std::cerr, "[INF] " + s_LogPrefix, fmt, std::forward<Args>(args)...);
    }

    static inline void Info(std::string_view msg)
    {
        LogTo(std::cerr, "[INF] " + s_LogPrefix, msg);
    }

    static inline void Info() {}

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

template<typename... Args>
static inline std::string Format(std::format_string<Args...> fmt, Args&&... args)
{
    return std::format(fmt, std::forward<Args>(args)...);
}

static inline std::string Format()
{
    static std::string empty = "";
    return empty;
}

static inline const std::string& Format(const std::string& str)
{
    return str;
}

static inline std::string_view Format(const std::string_view str)
{
    return str;
}

static inline const char* Format(const char* str)
{
    return str;
}

}   // namespace mlg

#define MLG_LOG_SCOPE_CONCAT_HELPER(a, b) a##b
#define MLG_LOG_SCOPE_CONCAT(a, b) MLG_LOG_SCOPE_CONCAT_HELPER(a, b)
#define MLG_LOG_SCOPE(...) mlg::LogScope MLG_LOG_SCOPE_CONCAT(logScope_, __LINE__)(__VA_ARGS__);

#define MLG_CHECK(expr, ...) \
    do { \
        if (!static_cast<bool>(expr)) { \
            mlg::Log::Error("[{}:{}]:({}) {}", __FILE__, __LINE__, #expr, mlg::Format(__VA_ARGS__)); \
            return mlg::Result<>::Fail; \
        } \
    } while (0);

/*
    * Collect all references to accessors
        * std::multimap<cgltf_accessor*, cgltf_accessor**>
        * Accessors may be shared, so each accessor could have several referents.
    * Confirm the number of keys is the same size as the list of accessors.
    * Create a collection of accessors that are referenced by primitives for the following properties
        * struct
        * {
        *     cgltf_primitive* primitive;
        *     cgltf_accessor* indices;
        *     cgltf_accessor* position;
        *     cgltf_accessor* normal;
        *     cgltf_accessor* texcoord0;
        *     cgltf_accessor* texcoord1;
        * }
    * Allocate index and vertex buffers (cgltf_buffer)
        * set offsets to zero
    * Allocate index and vertex cgltf_buffer_view.
        * Initialize them to point the buffers allocated above.
    * For each primitive
        * Decode and write u32 indices to index buffer.
            * If no indices then generate indices.
        * Decode and write properties (pos, normal, etc.) to vertex buffer.
        * Allocate and populate new cgltf_accessors for the decoded index and vertex data.
        * Update accessor pointers in the primitive.
        * Remove old accessor reference from the accessor collection.
    * Calculate size of data referenced by remaining accessors
    * For accessors that remain in the accessor collection:
        * Calcluate the total size of data referenced by the remaining accessors.
        * Allocate a new cgltf_buffer and cgltf_buffer_view for the remaining accessors.
        * For each accessor:
            * Copy the data from the old buffer to the new buffer, aligned to 4-byte boundaries.
            * Update the cgltf_accessor to point to the new buffer and buffer view.
*/

class Allocator
{
public:
    void* Allocate(const size_t size)
    {
        void* ptr = malloc(size);
        if(ptr)
        {
            m_Allocations.insert(ptr);
        }
        return ptr;
    }

    void Free(void* ptr)
    {
        if(ptr)
        {
            if(m_Allocations.find(ptr) != m_Allocations.end())
            {
                m_Allocations.erase(ptr);
                free(ptr);
            }
            else
            {
                mlg::Log::Error("Attempted to free memory that was not allocated by this allocator: {}", ptr);
            }
        }
    }

private:

    std::unordered_set<void*> m_Allocations;
};

class PrimitiveAttributes
{
public:
    uint32_t IndexInMesh;   // Index of the primitive within the mesh
    cgltf_primitive* Primitive;
    cgltf_mesh* Mesh;
    cgltf_accessor* Indices;
    cgltf_accessor* Position;
    cgltf_accessor* Normal;
    cgltf_accessor* Texcoord0;
    cgltf_accessor* Texcoord1;

    const char* GetMeshName() const
    {
        return (Mesh && Mesh->name) ? Mesh->name : "<unnamed>";
    }
};

// Collect primitives and the attributes we care about.
mlg::Result<>
CollectPrimitives(cgltf_data* data, std::vector<PrimitiveAttributes>& primitives)
{
    for(cgltf_size i = 0; i < data->meshes_count; ++i)
    {
        cgltf_mesh& mesh = data->meshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", mesh.name ? mesh.name : "<unnamed>", i);

        for(cgltf_size j = 0; j < mesh.primitives_count; ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            cgltf_primitive& primitive = mesh.primitives[j];

            if(primitive.type != cgltf_primitive_type_triangles)
            {
                mlg::Log::Warn("Only triangle primitives are supported. Ignoring.");
                continue;
            }

            if(!primitive.material)
            {
                mlg::Log::Warn("Primitive does not have a material. Ignoring.");
                continue;
            }

            if(primitive.attributes_count == 0)
            {
                mlg::Log::Warn("Primitive does not have any attributes. Ignoring.");
                continue;
            }

            if(primitive.targets_count != 0)
            {
                mlg::Log::Warn("Morph targets are not supported. Ignoring.");
                continue;
            }

            if(primitive.has_draco_mesh_compression)
            {
                mlg::Log::Warn("Draco mesh compression is not supported. Ignoring.");
                continue;
            }

            PrimitiveAttributes attrs//
            {
                .IndexInMesh = static_cast<uint32_t>(j),
                .Primitive = &primitive,
                .Mesh = &mesh,
                .Indices = primitive.indices,
            };

            for(cgltf_size k = 0; k < primitive.attributes_count; ++k)
            {
                cgltf_attribute& attribute = primitive.attributes[k];

                MLG_LOG_SCOPE("attr {}/{}", attribute.name ? attribute.name : "<unnamed>", k);

                if(attribute.data->is_sparse)
                {
                    mlg::Log::Warn(
                        "Sparse attribute data is not supported. Primitive will be ignored",
                        mesh.name ? mesh.name : "<unnamed>",
                        i,
                        j,
                        k);
                    break;
                }

                switch(attribute.type)
                {
                    case cgltf_attribute_type_position:
                        attrs.Position = attribute.data;
                        break;
                    case cgltf_attribute_type_normal:
                        attrs.Normal = attribute.data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        if(0 == attribute.index)
                        {
                            attrs.Texcoord0 = attribute.data;
                        }
                        else if(1 == attribute.index)
                        {
                            attrs.Texcoord1 = attribute.data;
                        }
                        else
                        {
                            mlg::Log::Warn(
                                "Unsupported texcoord index {} will be ignored",
                                attribute.index);
                        }
                        break;
                    default:
                        mlg::Log::Warn(
                            "Unsupported attribute type {} will be ignored",
                            std::to_underlying(attribute.type));
                        break;
                }
            }
            primitives.push_back(attrs);
        }
    }

    // Verify attribute counts match POSITION
    for(const auto& primitive : primitives)
    {
        MLG_LOG_SCOPE("mesh {}", primitive.GetMeshName());
        MLG_LOG_SCOPE("prim {}", primitive.IndexInMesh);

        const size_t posCount = primitive.Position ? primitive.Position->count : 0;
        const size_t normalCount = primitive.Normal ? primitive.Normal->count : posCount;
        const size_t texcoord0Count = primitive.Texcoord0 ? primitive.Texcoord0->count : posCount;
        const size_t texcoord1Count = primitive.Texcoord1 ? primitive.Texcoord1->count : posCount;

        MLG_CHECK(normalCount == posCount,
            "Normal count {} does not match vertex count {}",
            normalCount,
            posCount);
        MLG_CHECK(texcoord0Count == posCount,
            "Texcoord0 count {} does not match vertex count {}",
            texcoord0Count,
            posCount);
        MLG_CHECK(texcoord1Count == posCount,
            "Texcoord1 count {} does not match vertex count {}",
            texcoord1Count,
            posCount);
    }

    return mlg::Result<>::Ok;
}

// Index non-indexed primitives by generating an index buffer for them.
mlg::Result<>
BuildVertexBuffer(std::vector<PrimitiveAttributes>& primitives, std::vector<mlg::Vertex>& vertices)
{
    // Compute size of vertex buffer.
    size_t vertexCount = 0;
    for(const PrimitiveAttributes& primitive : primitives)
    {
        vertexCount += primitive.Position->count;
    }

    vertices.resize(vertexCount);

    mlg::Vertex* vtxDst = reinterpret_cast<mlg::Vertex*>(vertices.data());

    for(PrimitiveAttributes& primitive : primitives)
    {
        MLG_LOG_SCOPE("mesh {}", primitive.GetMeshName());
        MLG_LOG_SCOPE("prim {}", primitive.IndexInMesh);

        const size_t vbByteOffset =
            (reinterpret_cast<uint8_t*>(&vtxDst->pos.x) - reinterpret_cast<uint8_t*>(vertices.data()));

        for(cgltf_size j = 0; j < primitive.Position->count; ++j, ++vtxDst)
        {
            MLG_CHECK(cgltf_accessor_read_float(primitive.Position, j, &vtxDst->pos.x, 3),
                "Failed to read POSITION attribute");

            if(primitive.Normal)
            {
                MLG_CHECK(cgltf_accessor_read_float(primitive.Normal, j, &vtxDst->norm.x, 3),
                    "Failed to read NORMAL attribute");
            }

            if(primitive.Texcoord0)
            {
                MLG_CHECK(cgltf_accessor_read_float(primitive.Texcoord0, j, &vtxDst->uv[0].u, 2),
                    "Failed to read TEXCOORD_0 attribute");
            }
            else
            {
                vtxDst->uv[0].u = 0.0f;
                vtxDst->uv[0].v = 0.0f;
            }

            if(primitive.Texcoord1)
            {
                MLG_CHECK(cgltf_accessor_read_float(primitive.Texcoord1, j, &vtxDst->uv[1].u, 2),
                    "Failed to read TEXCOORD_1 attribute");
            }
            else
            {
                vtxDst->uv[1].u = 0.0f;
                vtxDst->uv[1].v = 0.0f;
            }
        }
    }

    return mlg::Result<>::Ok;
}

mlg::Result<>
BuildIndexBuffer(std::vector<PrimitiveAttributes>& primitives,
    std::vector<mlg::VertexIndex>& indices)
{
    // Compute size of index buffer.
    size_t indexCount = 0;
    for(const PrimitiveAttributes& primitive : primitives)
    {
        if(primitive.Indices)
        {
            indexCount += primitive.Indices->count;
        }
        else
        {
            indexCount += primitive.Position->count;
        }
    }

    indices.resize(indexCount);

    uint32_t* idxDst = reinterpret_cast<uint32_t*>(indices.data());

    for(PrimitiveAttributes& primitive : primitives)
    {
        MLG_LOG_SCOPE("mesh {}", primitive.GetMeshName());
        MLG_LOG_SCOPE("prim {}", primitive.IndexInMesh);

        const size_t ibByteOffset =
            (reinterpret_cast<uint8_t*>(&idxDst[0]) - reinterpret_cast<uint8_t*>(indices.data()));

        size_t idxCount = 0;

        if(primitive.Indices)
        {
            idxCount = primitive.Indices->count;

            const size_t count = cgltf_accessor_unpack_indices(primitive.Indices,
                idxDst,
                sizeof(uint32_t),
                idxCount);

            MLG_CHECK(count == idxCount,
                "Failed to unpack all indices for primitive");
        }
        else
        {
            idxCount = primitive.Position->count;
            for(size_t j = 0; j < idxCount; ++j)
            {
                idxDst[j] = static_cast<uint32_t>(j);
            }
        }

        idxDst += idxCount;
    }

    return mlg::Result<>::Ok;
}

mlg::Result<>
GenerateNormals(std::vector<PrimitiveAttributes>& primitives,
    std::vector<mlg::Vertex>& vertices,
    std::vector<mlg::VertexIndex>& indices)
{
    mlg::Vertex* vtx = vertices.data();
    const mlg::VertexIndex* idx = indices.data();

    for(const auto& primitive : primitives)
    {
        MLG_LOG_SCOPE("mesh {}", primitive.GetMeshName());
        MLG_LOG_SCOPE("prim {}", primitive.IndexInMesh);

        const size_t idxCount = primitive.Indices ? primitive.Indices->count : primitive.Position->count;

        if(!primitive.Normal)
        {
            for(size_t i = 0; i < primitive.Position->count; ++i)
            {
                vtx[i].norm = {0.0f, 0.0f, 0.0f};
            }

            for(size_t i = 0; i < idxCount; i += 3)
            {
                mlg::Vertex& v0 = vtx[idx[i + 0]];
                mlg::Vertex& v1 = vtx[idx[i + 1]];
                mlg::Vertex& v2 = vtx[idx[i + 2]];

                mlg::Vec3f edge1 = mlg::Sub(v1.pos, v0.pos);
                mlg::Vec3f edge2 = mlg::Sub(v2.pos, v0.pos);
                mlg::Vec3f edge3 = mlg::Sub(v2.pos, v1.pos);
                mlg::Vec3f normal0 = mlg::Normalize(mlg::Cross(edge1, edge2));
                mlg::Vec3f normal1 = mlg::Normalize(mlg::Cross(edge1, edge3));
                mlg::Vec3f normal2 = mlg::Normalize(mlg::Cross(edge2, edge3));

                if(Dot(normal0, normal0) < 0.0f || Dot(normal1, normal1) < 0.0f || Dot(normal2, normal2) < 0.0f)
                {
                        mlg::Log::Error("Generated normal has negative length");
                }

                v0.norm = mlg::Add(v0.norm, normal0);
                v1.norm = mlg::Add(v1.norm, normal1);
                v2.norm = mlg::Add(v2.norm, normal2);
            }

            for(size_t i = 0; i < primitive.Position->count; ++i)
            {
                vtx[i].norm = mlg::Normalize(vtx[i].norm);
            }
        }
    }

    return mlg::Result<>::Ok;
}

mlg::Result<>
BuildMeshBuffer(std::vector<PrimitiveAttributes>& primitives,
    std::vector<mlg::Mesh>& meshes)
{
    meshes.resize(primitives.size());

    mlg::Mesh* mesh = meshes.data();
    uint32_t idxOffset = 0;
    uint32_t baseVertex = 0;

    for(uint32_t i = 0; i < primitives.size(); ++i, ++mesh)
    {
        const PrimitiveAttributes& primitive = primitives[i];
        MLG_LOG_SCOPE("mesh {}", primitive.GetMeshName());
        MLG_LOG_SCOPE("prim {}", primitive.IndexInMesh);

        *mesh = //
            {
                .NodeIndex = i,
                .IndexCount = static_cast<uint32_t>(
                    primitive.Indices ? primitive.Indices->count : primitive.Position->count),
                .FirstIndex = idxOffset,
                .BaseVertex = baseVertex,
            };
        idxOffset += mesh->IndexCount;
        baseVertex += static_cast<uint32_t>(primitive.Position->count);
    }

    return mlg::Result<>::Ok;
}

mlg::Result<>
BuildNodeBuffer(const cgltf_data* data, std::vector<mlg::Mat44>& nodes)
{
    auto countNodes = [](this auto&& self, const cgltf_node* node) -> size_t
    {
        if(!node->mesh && node->children_count == 0)
        {
            // This could be a procedurally generated mesh, e.g. the leaves of a tree.
            // In blender the following steps could be used to convert to a mesh:
            // 1. Select the node in the outliner.
            // 2. In the "Layout" editer select the "Object" menu and choose "Apply -> Make Instances Real".
            //      This will generate meshes and possibly mesh instances.
            // 3. In the outliner select all the resulting objects.
            // 4. In the "Layout" editer select the "Object" menu and choose "Convert -> Mesh".
            //      This will materialize instances into stand alone meshes.
            // 5. In the outliner select all the resulting objects (meshes).
            // 6. In the "Layout" editer select the "Object" menu and choose "Join".
            //      This will join all the selected meshes into a single mesh.

            const char* type;
            if(node->camera)
            {
                type = "camera";
            }
            else if(node->light)
            {
                type = "light";
            }
            else if(node->skin)
            {
                type = "skin";
            }
            else if(node->weights)
            {
                type = "weights";
            }
            else
            {
                type = "unknown";
            }

            mlg::Log::Warn("Node \"{}\" ({}) has no mesh and no children.  Ignoring.",
                node->name ? node->name : "<unnamed>",
                type);

            return 0;
        }

        size_t count = 0;
        for(cgltf_size i = 0; i < node->children_count; ++i)
        {
            count += self(node->children[i]);
        }

        if(count > 0 || node->mesh)
        {
            ++count;
        }

        return count;
    };

    // Count nodes
    size_t nodeCount = 0;
    for(cgltf_size i = 0; i < data->scene->nodes_count; ++i)
    {
        const cgltf_node& node = *data->scene->nodes[i];
        nodeCount += countNodes(&node);
    }

    nodes.resize(nodeCount);
    mlg::Mat44* nextNode = nodes.data();

    auto collectNodes = [](this auto&& self, const cgltf_node* node, mlg::Mat44* outNodes) -> mlg::Mat44*
    {
        if(!node->mesh && node->children_count == 0)
        {
            return outNodes;
        }

        mlg::Mat44 m;
        cgltf_node_transform_local(node, m.m);
        *outNodes++ = mlg::Transpose(m);

        mlg::Mat44* childNodes = outNodes;

        for(cgltf_size i = 0; i < node->children_count; ++i)
        {
            childNodes = self(node->children[i], childNodes);
        }

        if(childNodes > outNodes)
        {
            return childNodes;
        }
        else if(!node->mesh)
        {
            --outNodes;
        }

        return outNodes;
    };

    for(cgltf_size i = 0; i < data->scene->nodes_count; ++i)
    {
        const cgltf_node& node = *data->scene->nodes[i];
        nextNode = collectNodes(&node, nextNode);
    }

    return mlg::Result<>::Ok;
}

void* alloc_func(void* user_data, size_t size)
{
    Allocator* allocator = static_cast<Allocator*>(user_data);
    return allocator->Allocate(size);
}

void free_func(void* user_data, void* ptr)
{
    Allocator* allocator = static_cast<Allocator*>(user_data);
    allocator->Free(ptr);
}

int
main(int argc, char** argv)
{
    static constexpr const char* SCENE_PATH =
        "C:/Users/kbaca/Downloads/HiddenAlley3/ph_hidden_alley.gltf";
        //"C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";

    Allocator allocator;

    cgltf_options options{};
    options.memory = //
        {
            .alloc_func = alloc_func,
            .free_func = free_func,
            .user_data = &allocator,
        };

    cgltf_data* data = NULL;
    cgltf_result parseResult = cgltf_parse_file(&options, SCENE_PATH, &data);
    if(parseResult != cgltf_result_success)
    {
        mlg::Log::Error("Failed to parse glTF file {}", SCENE_PATH);
        return -1;
    }

    if(data->scenes_count == 0)
    {
        mlg::Log::Error("No scenes found in glTF file {}", SCENE_PATH);
        return -1;
    }

    if(data->scenes_count > 1)
    {
        mlg::Log::Error("Multiple scenes found in glTF file {}, only one scene is supported", SCENE_PATH);
        return -1;
    }

    for(cgltf_size i = 0; i < data->buffer_views_count; ++i)
    {
        const cgltf_buffer_view& buffer_view = data->buffer_views[i];
        if(buffer_view.has_meshopt_compression)
        {
            mlg::Log::Error("Unsupported meshopt compression in buffer view {}", i);
            return -1;
        }
    }

    std::vector<PrimitiveAttributes> primitiveAttributes;
    if(!CollectPrimitives(data, primitiveAttributes))
    {
        mlg::Log::Error("Failed to collect primitive attributes from glTF data");
        return -1;
    }

    cgltf_result loadBuffersResult = cgltf_load_buffers(&options, data, SCENE_PATH);
    if(loadBuffersResult != cgltf_result_success)
    {
        mlg::Log::Error("Failed to load buffers for glTF file {}", SCENE_PATH);
        return -1;
    }

    std::vector<mlg::Vertex> vertices;
    std::vector<mlg::VertexIndex> indices;
    std::vector<mlg::Mesh> meshes;
    std::vector<mlg::Mat44> nodes;
    std::vector<mlg::Material> materials;

    if(!BuildVertexBuffer(primitiveAttributes, vertices))
    {
        mlg::Log::Error("Failed to build vertex buffer for primitives");
        return -1;
    }

    if(!BuildIndexBuffer(primitiveAttributes, indices))
    {
        mlg::Log::Error("Failed to build index buffer for primitives");
        return -1;
    }

    if(!GenerateNormals(primitiveAttributes, vertices, indices))
    {
        mlg::Log::Error("Failed to generate normals for primitives");
        return -1;
    }

    if(!BuildMeshBuffer(primitiveAttributes, meshes))
    {
        mlg::Log::Error("Failed to build mesh buffer for primitives");
        return -1;
    }

    if(!BuildNodeBuffer(data, nodes))
    {
        mlg::Log::Error("Failed to build node buffer for primitives");
        return -1;
    }

    /*if(!BuildMaterialBuffer(primitiveAttributes, materials))
    {
        mlg::Log::Error("Failed to build material buffer for primitives");
        return -1;
    }*/

    return 0;
}