#define _CRT_SECURE_NO_WARNINGS

#include "ImageLoader.h"

#include "Error.h"

#include <png.h>
#include <cstdio>
#include <vector>
#include <string>

//Isolate setjmp to C code and disable warning C4611: interaction between '_setjmp' and C++ object destruction is non-portable 
#pragma warning(push)
#pragma warning(disable:4611)
extern "C"
{
    static bool ReadPNGFile(FILE* fp, png_structp png_ptr, png_infop info_ptr)
    {
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            return false;
        }

        png_init_io(png_ptr, fp);
        png_set_sig_bytes(png_ptr, 8);

        png_read_info(png_ptr, info_ptr);

        png_uint_32 width;
        png_uint_32 height;
        int bit_depth;
        int color_type;
        int interlace;
        int compression;
        int filter;

        png_get_IHDR(
            png_ptr,
            info_ptr,
            &width,
            &height,
            &bit_depth,
            &color_type,
            &interlace,
            &compression,
            &filter
        );

        // --- Normalize to RGBA8 ---

        if (bit_depth == 16)
        {
            png_set_strip_16(png_ptr);
        }

        if (color_type == PNG_COLOR_TYPE_PALETTE)
        {
            png_set_palette_to_rgb(png_ptr);
        }

        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        {
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }

        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        {
            png_set_tRNS_to_alpha(png_ptr);
        }

        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        {
            png_set_gray_to_rgb(png_ptr);
        }

        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

        if (interlace != PNG_INTERLACE_NONE)
        {
            png_set_interlace_handling(png_ptr);
        }

        png_read_update_info(png_ptr, info_ptr);

        return true;
    }

    static bool ReadPNGRows(png_structp png_ptr, std::vector<png_bytep>& row_pointers)
    {
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            return false;
        }

        png_read_image(png_ptr, row_pointers.data());
        png_read_end(png_ptr, nullptr);

        return true;
    }
}//extern "C"
#pragma warning(pop)

std::expected<RefPtr<Image>, Error> ImageLoader::LoadPng(const std::string_view path)
{
    FILE* fp = fopen(path.data(), "rb");
    expect(fp, "Failed to open file: {}", path);

    png_byte header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8))
    {
        fclose(fp);
        return std::unexpected(Error("Not a valid PNG file: {}", path.data()));
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
    {
        fclose(fp);
        return std::unexpected(Error("Failed to create png read struct"));
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        fclose(fp);
        return std::unexpected(Error("Failed to create png info struct"));
    }

    if (!ReadPNGFile(fp, png_ptr, info_ptr))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(fp);
        return std::unexpected(Error("libpng error during read"));
    }

    png_size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    int interlace;
    int compression;
    int filter;

    png_get_IHDR(
        png_ptr,
        info_ptr,
        &width,
        &height,
        &bit_depth,
        &color_type,
        &interlace,
        &compression,
        &filter
    );

    RefPtr<Image> img = Image::Create(width, height);

    std::vector<png_bytep> row_pointers(height);
    for (png_uint_32 y = 0; y < height; ++y)
    {
        row_pointers[y] = img->Pixels + y * rowbytes;
    }

    const bool readRows = ReadPNGRows(png_ptr, row_pointers);

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(fp);

    return readRows ? img : nullptr;
}
