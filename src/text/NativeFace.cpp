#include "text/NativeFace.h"

#include "text/FontFile.h"

#if defined(PRISM_NATIVE_TEXT)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include <hb.h>
#endif

#include <algorithm>
#include <climits>
#include <cmath>

namespace prism::text {
#if defined(PRISM_NATIVE_TEXT)
struct NativeFace::Impl {
    std::vector<std::uint8_t> bytes;
    std::uint16_t unitsPerEm = 1000;
    hb_blob_t *blob = nullptr;
    hb_face_t *face = nullptr;
    hb_font_t *font = nullptr;
    FT_Library library = nullptr;
    FT_Face ftFace = nullptr;

    ~Impl() {
        if (ftFace != nullptr) {
            FT_Done_Face(ftFace);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
};

bool nativeTextAvailable() noexcept { return true; }

std::shared_ptr<NativeFace> NativeFace::create(const FontFile &font) {
    std::shared_ptr<NativeFace> result(new NativeFace());
    result->impl_ = std::make_unique<Impl>();
    Impl &s = *result->impl_;
    const auto bytes = font.bytes();
    s.bytes.assign(bytes.begin(), bytes.end());
    s.unitsPerEm = font.metrics().unitsPerEm;
    if (FT_Init_FreeType(&s.library) != 0 ||
        FT_New_Memory_Face(s.library, s.bytes.data(), static_cast<FT_Long>(s.bytes.size()), 0,
                           &s.ftFace) != 0) {
        return nullptr;
    }
    FT_Library_SetLcdFilter(s.library, FT_LCD_FILTER_DEFAULT);

    s.blob = hb_blob_create(reinterpret_cast<const char *>(s.bytes.data()),
                            static_cast<unsigned>(s.bytes.size()), HB_MEMORY_MODE_READONLY, nullptr,
                            nullptr);
    s.face = hb_face_create(s.blob, 0);
    s.font = hb_font_create(s.face);
    hb_font_set_scale(s.font, s.unitsPerEm, s.unitsPerEm);
    if (hb_face_get_glyph_count(s.face) == 0) {
        return nullptr;
    }

    const auto variation = font.variation();
    if (!variation.empty()) {
        std::vector<hb_variation_t> settings;
        for (const auto &[tag, value] : variation) {
            settings.push_back({tag, static_cast<float>(value)});
        }
        hb_font_set_variations(s.font, settings.data(), static_cast<unsigned>(settings.size()));

        FT_MM_Var *master = nullptr;
        if (FT_Get_MM_Var(s.ftFace, &master) == 0) {
            std::vector<FT_Fixed> coords(master->num_axis);
            for (FT_UInt a = 0; a < master->num_axis; ++a) {
                double value = master->axis[a].def / 65536.0;
                for (const auto &[tag, requested] : variation) {
                    if (master->axis[a].tag == tag) {
                        value = requested;
                    }
                }
                coords[a] = static_cast<FT_Fixed>(std::lround(value * 65536.0));
            }
            FT_Set_Var_Design_Coordinates(s.ftFace, master->num_axis, coords.data());
            FT_Done_MM_Var(s.library, master);
        }
    }
    return result;
}

NativeFace::~NativeFace() = default;

void NativeFace::shape(std::span<const char32_t> codepoints, std::uint32_t firstCluster,
                       bool rightToLeft, std::span<const std::uint32_t> features, float size,
                       std::vector<NativeGlyph> &out) const {
    const Impl &s = *impl_;
    hb_buffer_t *buffer = hb_buffer_create();
    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        hb_buffer_add(buffer, codepoints[i], firstCluster + static_cast<std::uint32_t>(i));
    }
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_direction(buffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);

    std::vector<hb_feature_t> requested;
    for (const std::uint32_t tag : features) {
        requested.push_back({tag, 1, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END});
    }
    hb_shape(s.font, buffer, requested.data(), static_cast<unsigned>(requested.size()));

    unsigned count = 0;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
    const float scale = size / static_cast<float>(s.unitsPerEm);
    for (unsigned i = 0; i < count; ++i) {
        out.push_back({static_cast<std::uint16_t>(infos[i].codepoint), infos[i].cluster,
                       static_cast<float>(positions[i].x_advance) * scale,
                       static_cast<float>(positions[i].x_offset) * scale,
                       static_cast<float>(positions[i].y_offset) * scale});
    }
    hb_buffer_destroy(buffer);
}

bool NativeFace::rasterize(std::uint16_t glyph, float pixelSize, std::uint32_t phase, bool lcd,
                           GlyphBitmap &bitmap) const {
    const Impl &s = *impl_;
    if (FT_Set_Char_Size(s.ftFace, 0, static_cast<FT_F26Dot6>(std::lround(pixelSize * 64)), 72,
                         72) != 0 ||
        FT_Load_Glyph(s.ftFace, glyph,
                      FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT) != 0) {
        return false;
    }
    FT_GlyphSlot slot = s.ftFace->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
        return false;
    }
    FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(phase * 64 / 4), 0);
    if (FT_Render_Glyph(slot, lcd ? FT_RENDER_MODE_LCD : FT_RENDER_MODE_NORMAL) != 0) {
        return false;
    }

    const FT_Bitmap &source = slot->bitmap;
    const std::uint32_t height = source.rows;
    const std::uint32_t width = height == 0 ? 0 : lcd ? source.width / 3 : source.width;
    bitmap.width = width;
    bitmap.height = width == 0 ? 0 : height;
    bitmap.channels = lcd ? 4 : 1;
    bitmap.left = static_cast<float>(slot->bitmap_left);
    bitmap.top = -static_cast<float>(slot->bitmap_top);
    bitmap.scale = 1;
    bitmap.texels.assign(static_cast<std::size_t>(width) * height * bitmap.channels, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        const unsigned char *row = source.buffer + static_cast<std::ptrdiff_t>(y) * source.pitch;
        for (std::uint32_t x = 0; x < width; ++x) {
            float *texel =
                &bitmap.texels[(static_cast<std::size_t>(y) * width + x) * bitmap.channels];
            if (!lcd) {
                texel[0] = row[x] / 255.0f;
                continue;
            }
            texel[0] = row[3 * x] / 255.0f;
            texel[1] = row[3 * x + 1] / 255.0f;
            texel[2] = row[3 * x + 2] / 255.0f;
            texel[3] = (texel[0] + texel[1] + texel[2]) / 3;
        }
    }
    return true;
}
#else
struct NativeFace::Impl {};

bool nativeTextAvailable() noexcept { return false; }

std::shared_ptr<NativeFace> NativeFace::create(const FontFile &) { return nullptr; }

NativeFace::~NativeFace() = default;

void NativeFace::shape(std::span<const char32_t>, std::uint32_t, bool,
                       std::span<const std::uint32_t>, float, std::vector<NativeGlyph> &) const {}

bool NativeFace::rasterize(std::uint16_t, float, std::uint32_t, bool, GlyphBitmap &) const {
    return false;
}
#endif

NativeFace *FontFile::native() const {
    if (!nativeTried_) {
        nativeTried_ = true;
        native_ = NativeFace::create(*this);
    }
    return native_.get();
}
}
