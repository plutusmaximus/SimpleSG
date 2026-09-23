
#include "BoundsCheck.h"
#include "GltfLoader.h"
#include "LevelDefs.h"
#include "Log.h"
#include "ResourceBundle.h"
#include "Result.h"
#include "scope_exit.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <latch>
#include <limits>
#include <set>
#include <span>
#include <stb_image.h>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

void
Usage()
{
    std::cout << "Usage: cook -i <input file> -o <output directory>" << std::endl;
}

/// Command line arguments for the cook tool.
struct CmdLinArgs
{
    const char* InputFile{ nullptr };
    const char* OutputDir{ nullptr };

    bool IsValid() const { return InputFile != nullptr && OutputDir != nullptr; }
};

/// Input and output directories for the cook tool.
struct CookDirs
{
    std::filesystem::path InputDir;
    std::filesystem::path OutputDir;

    /// Creates a CookDirs instance from command line arguments.
    static Result<CookDirs> FromArgs(const CmdLinArgs& args);
};

/// Number of bytes per pixel for textures.
constexpr uint32_t kTextureBytesPerPixel = 4;

/// Texture row alignment in bytes.
constexpr uint32_t kTextureRowAlignment = 256;

/// Header for a cooked texture file.
struct CookedTextureHeader
{
    uint32_t Version{ 1 };
    uint32_t Width{ 0 };
    uint32_t Height{ 0 };
};

static_assert(std::is_trivially_copyable_v<CookedTextureHeader>);
static_assert(std::is_standard_layout_v<CookedTextureHeader>);
static_assert(sizeof(CookedTextureHeader) == 12); // NOLINT(readability-magic-numbers)

static_assert(kTextureBytesPerPixel == 4);
static_assert(sizeof(CookedTextureHeader) % kTextureBytesPerPixel == 0);
// Ensures that the texture row alignment is a power of two.
static_assert(std::has_single_bit(kTextureRowAlignment));
static_assert(kTextureRowAlignment % kTextureBytesPerPixel == 0);

/// Parameters for a worker that cooks a texture.
struct CookTextureWorkerParams
{
    const CookDirs* CookDirs;
    std::string_view InTexturePath;
    std::string OutTexturePath;
    std::latch* Latch{ nullptr };
    Result<> Result;
};

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
CmdLinArgs
ParseArgs(int argc, char** argv)
{
    CmdLinArgs args;
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-i") == 0 && i + 1 < argc)
        {
            args.InputFile = argv[++i];
        }
        else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            args.OutputDir = argv[++i];
        }
    }
    return args;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

Result<CookDirs>
CookDirs::FromArgs(const CmdLinArgs& args)
{
    std::filesystem::path inDir = args.InputFile;
    inDir = inDir.parent_path();

    if(inDir.empty())
    {
        std::error_code ec;
        inDir = std::filesystem::current_path(ec);
        MLG_CHECKV(!ec, "Failed to get current working directory: {}", ec.message());
    }

    const std::filesystem::path outDir = args.OutputDir;

    MLG_CHECKV(!outDir.empty(), "Output directory is not specified");

    std::error_code ec;
    const std::filesystem::file_status outDirStatus = std::filesystem::status(outDir, ec);

    MLG_CHECKV(!ec, "Failed to get status of output directory: {}", ec.message());

    ec.clear();

    MLG_CHECKV(std::filesystem::exists(outDirStatus),
        "Output directory does not exist: {}",
        outDir.string());

    MLG_CHECKV(std::filesystem::is_directory(outDirStatus),
        "Output path is not a directory: {}",
        outDir.string());

    return CookDirs //
        {
            .InputDir = inDir,
            .OutputDir = outDir,
        };
}

/// Aligns the given value up to the specified alignment.
uint32_t
AlignUp(uint32_t value, uint32_t alignment)
{
    MLG_ABORTIF(alignment == 0, "Alignment must be non-zero");
    const uint32_t sum =
        BoundsCheck::Sum(value, alignment - 1, std::numeric_limits<uint32_t>::max());

    return sum & ~(alignment - 1);
}

Result<std::vector<std::string>>
CollectTexturePaths(const PropKitDef& propKitDef)
{
    std::set<std::string> uniqueTexturePaths;
    for(const ModelDef& modelDef : propKitDef.ModelDefs)
    {
        for(const MeshDef& meshDef : modelDef.MeshDefs)
        {
            const std::string& texPath = meshDef.MaterialDef.BaseTexturePath;

            if(!texPath.empty())
            {
                uniqueTexturePaths.insert(texPath);
            }
        }
    }

    std::vector<std::string> texturePaths(uniqueTexturePaths.begin(), uniqueTexturePaths.end());

    return texturePaths;
}

Result<>
ReplaceTexturePaths(PropKitDef& propKitDef,
    const std::unordered_map<std::string, std::string>& texturePathReplacements)
{
    for(ModelDef& modelDef : propKitDef.ModelDefs)
    {
        for(MeshDef& meshDef : modelDef.MeshDefs)
        {
            std::string& texPath = meshDef.MaterialDef.BaseTexturePath;

            if(!texPath.empty())
            {
                const auto it = texturePathReplacements.find(texPath);

                MLG_CHECKV(it != texturePathReplacements.end(),
                    "Texture path replacement not found for: {}",
                    texPath);

                texPath = it->second;
            }
        }
    }
    return Result<>::Ok;
}

Result<>
WriteFile(const std::filesystem::path& path, std::span<const std::byte> data)
{
    MLG_CHECK(data.size() <= static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    MLG_CHECKV(stream.is_open(), "Failed to open output file: {}", path.string());

    const void* p = data.data();

    stream.write(static_cast<const char*>(p), static_cast<std::streamsize>(data.size()));

    MLG_CHECKV(stream.good(), "Failed to write output file: {}", path.string());

    stream.close();
    MLG_CHECKV(!stream.fail(), "Failed to close output file: {}", path.string());

    return Result<>::Ok;
}

Result<std::vector<std::byte>>
LoadImage(const CookDirs& cookDirs, const std::string_view texturePath)
{
    const std::filesystem::path textureFullPath = (cookDirs.InputDir / texturePath);
    std::ifstream stream(textureFullPath, std::ios::binary);
    MLG_CHECKV(stream.is_open(),
        "Failed to open image file for reading: {}",
        textureFullPath.string());

    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(textureFullPath, ec);
    MLG_CHECKV(!ec, "Failed to get image file size: {}", ec.message());

    // Make sure the file size fits within an int for stb_image.
    MLG_CHECKV(fileSize <= static_cast<uintmax_t>(std::numeric_limits<int>::max()),
        "Image file too large: {}",
        textureFullPath.string());

    MLG_CHECKV(fileSize > 0, "Image file is empty: {}", textureFullPath.string());

    std::vector<char> fileData;

    MLG_CHECKV(fileSize <= fileData.max_size(),
        "Image file too large: {}",
        textureFullPath.string());

    fileData.resize(static_cast<size_t>(fileSize));

    stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
    MLG_CHECKV(stream.good(), "Failed to read image file: {}", textureFullPath.string());

    MLG_INFO("Loading image: {}", textureFullPath.string());

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;
    const int len = static_cast<int>(fileData.size());
    const void* p = fileData.data();
    stbi_uc* imgData = stbi_load_from_memory(static_cast<const stbi_uc*>(p),
        len,
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        kTextureBytesPerPixel);

    MLG_CHECKV(imgData, "Failed to decode image: {}", stbi_failure_reason());

    // Free the memory used by the original image data as it is no longer needed.
    std::vector<char>().swap(fileData);

    MLG_DEFER
    {
        stbi_image_free(imgData);
    };

    MLG_CHECKV(imgNumChannels > 0, "Invalid number of image channels: {}", imgNumChannels);

    MLG_CHECKV(imgWidth > 0 && imgHeight > 0,
        "Invalid image dimensions: {}x{}",
        imgWidth,
        imgHeight);

    const uint32_t uImgWidth = static_cast<uint32_t>(imgWidth);
    const uint32_t uImgHeight = static_cast<uint32_t>(imgHeight);

    const uint32_t bytesPerSrcRow =
        BoundsCheck::Mul(uImgWidth, kTextureBytesPerPixel, std::numeric_limits<uint32_t>::max());

    const uint32_t bytesPerDstRow = AlignUp(bytesPerSrcRow, kTextureRowAlignment);
    const uint32_t dataSize =
        BoundsCheck::Mul(bytesPerDstRow, uImgHeight, std::numeric_limits<uint32_t>::max());

    std::vector<std::byte> textureData(dataSize + sizeof(CookedTextureHeader));

    const CookedTextureHeader header //
        {
            .Width = uImgWidth,
            .Height = uImgHeight,
        };

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::byte* dst = textureData.data();

    MLG_INFO("Cooking texture: {} ({}x{})", textureFullPath.string(), uImgWidth, uImgHeight);

    std::memcpy(dst, &header, sizeof(CookedTextureHeader));

    dst += sizeof(CookedTextureHeader);
    const stbi_uc* src = imgData;

    for(uint32_t row = 0; row < uImgHeight; ++row, dst += bytesPerDstRow, src += bytesPerSrcRow)
    {
        std::memcpy(dst, src, bytesPerSrcRow);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    return textureData;
}

Result<>
WriteTexture(const CookDirs& cookDirs,
    const std::string_view texturePath,
    const std::span<const std::byte> textureData)
{
    const std::filesystem::path outPath = cookDirs.OutputDir / std::filesystem::path(texturePath);

    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    MLG_CHECKV(!ec, "Failed to create output directory: {}", ec.message());

    MLG_INFO("Writing cooked texture to: {}", outPath.string());

    MLG_CHECK(WriteFile(outPath, textureData));

    MLG_INFO("Wrote texture: {}", outPath.string());

    return Result<>::Ok;
}

Result<>
CookTexture(CookTextureWorkerParams& params)
{
    const CookDirs& cookDirs = *params.CookDirs;

    auto textureData = LoadImage(cookDirs, params.InTexturePath);

    MLG_CHECK(textureData);

    MLG_CHECK(WriteTexture(cookDirs, params.OutTexturePath, *textureData));

    return Result<>::Ok;
}

void
CookTextureWorker(void* arg)
{
    CookTextureWorkerParams* params = static_cast<CookTextureWorkerParams*>(arg);
    if(params)
    {
        params->Result = CookTexture(*params);

        params->Latch->count_down();
    }
}

Result<>
WriteResourceBundle(const CookDirs& cookDirs,
    const std::string_view inputFile,
    const ResourceBundle& resourceBundle)
{
    const std::filesystem::path inPath = inputFile;
    std::filesystem::path outputFilePath = cookDirs.OutputDir / inPath.filename();
    outputFilePath.replace_extension(".bin");

    const std::span<const char> buffer = resourceBundle.GetBuffer();
    MLG_CHECK(WriteFile(outputFilePath, std::as_bytes(buffer)));

    MLG_INFO("Wrote resource bundle: {}", outputFilePath.string());

    return Result<>::Ok;
}

Result<>
Cook(const CmdLinArgs& args, ThreadPool& threadPool)
{
    Timer timer;
    timer.Start();

    MLG_CHECK(args.IsValid());

    PropKitDef propKitDef;
    LevelDef levelDef;

    MLG_CHECK(GltfLoader::Load(args.InputFile, propKitDef, levelDef));

    auto texPaths = CollectTexturePaths(propKitDef);
    MLG_CHECK(texPaths);

    std::unordered_map<std::string, std::string> texturePathReplacements;
    for(const std::string_view texPath : *texPaths)
    {
        std::filesystem::path inPath = texPath;
        inPath.replace_extension(".ctex");
        texturePathReplacements.emplace(texPath, inPath.string());
    }

    MLG_CHECK(ReplaceTexturePaths(propKitDef, texturePathReplacements));

    const Result<CookDirs> cookDirs = CookDirs::FromArgs(args);
    MLG_CHECK(cookDirs);

    std::deque<CookTextureWorkerParams> cookTextureParamsQueue;

    std::latch cookTextureLatch(static_cast<int>(texPaths->size()));

    MLG_DEFER
    {
        // Wait for all texture cooking tasks to complete before exiting the scope.
        cookTextureLatch.wait();
    };

    for(const std::string& inTexPath : *texPaths)
    {
        const std::string& outTexPath = texturePathReplacements.at(inTexPath);
        CookTextureWorkerParams& params = cookTextureParamsQueue.emplace_back(&*cookDirs,
            inTexPath,
            outTexPath,
            &cookTextureLatch);

        if(!threadPool.Enqueue(CookTextureWorker, &params))
        {
            params.Result = Result<>::Fail;
            params.Latch->count_down();
        }
    }

    ResourceBundleBuilder builder;

    auto resourceBundle = builder.Build(levelDef, propKitDef);
    MLG_CHECK(resourceBundle);

    cookTextureLatch.wait();

    Result<> cookTexResult = Result<>::Ok;

    for(const CookTextureWorkerParams& params : cookTextureParamsQueue)
    {
        if(!params.Result)
        {
            cookTexResult = params.Result;
        }
    }

    MLG_CHECK(cookTexResult, "One or more texture cooking tasks failed");

    MLG_CHECK(WriteResourceBundle(*cookDirs, args.InputFile, *resourceBundle));

    timer.Stop();
    MLG_INFO("Cook completed in {} ms", std::trunc(timer.GetElapsedSeconds() * 1000));

    return Result<>::Ok;
}

} // namespace

int
main(int argc, char** argv)
{
    Log::SetLevel(Log::Level::Trace);
    ThreadPool threadPool;

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if(ec)
    {
        MLG_ERROR("Failed to get current working directory: {}", ec.message());
        return 1;
    }
    MLG_INFO("Current working directory: {}", cwd.string());

    const CmdLinArgs args = ParseArgs(argc, argv);

    const Result<> result = Cook(args, threadPool);
    if(!result)
    {
        Usage();
        return 1;
    }

    return 0;
}