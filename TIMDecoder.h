#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <glad/glad.h>

struct ClutPage {
    int      colors = 0;
    uint32_t palette[256];
};

struct TimDecoded {
    int      bpp      = 0;
    bool     has_clut = false;
    int      img_w    = 0;
    int      img_h    = 0;
    int      clut_w   = 0;
    int      clut_h   = 0;
    std::vector<ClutPage> clut_pages;
    std::vector<uint8_t>  index_data;
    std::vector<uint32_t> direct_rgba;
};

#pragma pack(push, 1)
struct TimHeader      { uint32_t magic; uint32_t flags; };
struct TimClutHeader  { uint32_t length; uint16_t x, y, w, h; };
struct TimImageHeader { uint32_t length; uint16_t x, y, w, h; };
#pragma pack(pop)

inline uint8_t ps1_5to8(uint16_t c) {
    return (uint8_t)((c & 0x1F) << 3 | (c & 0x1F) >> 2);
}

inline uint32_t ps1_color(uint16_t c) {
    return (uint32_t)ps1_5to8(c)
        | ((uint32_t)ps1_5to8(c >> 5)  << 8)
        | ((uint32_t)ps1_5to8(c >> 10) << 16)
        | ((c == 0) ? 0x00000000u : 0xFF000000u);
}

inline TimDecoded decode_tim_data(const uint8_t* data, size_t len) {
    TimDecoded out{};
    if (len < sizeof(TimHeader)) return out;

    const TimHeader* hdr = (const TimHeader*)data;
    if (hdr->magic != 0x10) return out;

    uint32_t bpp_code = hdr->flags & 0x7;
    out.bpp      = bpp_code == 0 ? 4 : bpp_code == 1 ? 8 : bpp_code == 2 ? 16 : 24;
    out.has_clut = (hdr->flags >> 3) & 1;

    const uint8_t* ptr = data + sizeof(TimHeader);
    const uint8_t* end = data + len;

    if (out.has_clut) {
        if (ptr + sizeof(TimClutHeader) > end) return out;
        const TimClutHeader* ch = (const TimClutHeader*)ptr;
        ptr += sizeof(TimClutHeader);

        out.clut_w = ch->w;
        out.clut_h = ch->h;

        int colors_per_page = (out.bpp == 4) ? 16 : 256;
        int total_colors    = ch->w * ch->h;
        int num_pages       = (total_colors + colors_per_page - 1) / colors_per_page;

        out.clut_pages.resize(num_pages);
        for (int p = 0; p < num_pages; p++) {
            ClutPage& pg = out.clut_pages[p];
            pg.colors = std::min(colors_per_page, total_colors - p * colors_per_page);
            for (int c = 0; c < pg.colors; c++) {
                int abs_idx = p * colors_per_page + c;
                if (ptr + abs_idx * 2 + 2 > end) break;
                uint16_t raw; memcpy(&raw, ptr + abs_idx * 2, 2);
                pg.palette[c] = ps1_color(raw);
            }
        }
        ptr += total_colors * 2;
    }

    if (ptr + sizeof(TimImageHeader) > end) return out;
    const TimImageHeader* ih = (const TimImageHeader*)ptr;
    ptr += sizeof(TimImageHeader);

    int words_w = ih->w;
    out.img_h   = ih->h;

    if      (out.bpp ==  4) out.img_w = words_w * 4;
    else if (out.bpp ==  8) out.img_w = words_w * 2;
    else if (out.bpp == 16) out.img_w = words_w;
    else                    out.img_w = words_w * 2 / 3;

    size_t px_bytes = (size_t)words_w * out.img_h * 2;
    if (ptr + px_bytes > end) return out;

    if (out.has_clut) {
        out.index_data.resize(out.img_w * out.img_h);
        if (out.bpp == 4) {
            for (int y = 0; y < out.img_h; y++)
                for (int x = 0; x < words_w; x++) {
                    uint16_t w16; memcpy(&w16, ptr + (y * words_w + x) * 2, 2);
                    for (int s = 0; s < 4; s++) {
                        int px = x * 4 + s;
                        if (px < out.img_w)
                            out.index_data[y * out.img_w + px] = (w16 >> (s * 4)) & 0xF;
                    }
                }
        } else {
            for (int y = 0; y < out.img_h; y++)
                for (int x = 0; x < words_w; x++) {
                    uint16_t w16; memcpy(&w16, ptr + (y * words_w + x) * 2, 2);
                    int px0 = x * 2;
                    if (px0 < out.img_w)     out.index_data[y * out.img_w + px0]     = w16 & 0xFF;
                    if (px0 + 1 < out.img_w) out.index_data[y * out.img_w + px0 + 1] = (w16 >> 8) & 0xFF;
                }
        }
    } else {
        out.direct_rgba.resize(out.img_w * out.img_h, 0xFF000000u);
        if (out.bpp == 16) {
            for (int i = 0; i < out.img_w * out.img_h; i++) {
                uint16_t c; memcpy(&c, ptr + i * 2, 2);
                out.direct_rgba[i] = ps1_color(c);
            }
        } else {
            const uint8_t* px = ptr;
            for (int i = 0; i < out.img_w * out.img_h && px + 3 <= end; i++, px += 3)
                out.direct_rgba[i] = (uint32_t)px[0] | ((uint32_t)px[1] << 8)
                | ((uint32_t)px[2] << 16) | 0xFF000000u;
        }
    }

    return out;
}

inline GLuint build_gl_texture(const TimDecoded& tim, int clut_page_idx) {
    if (tim.img_w == 0 || tim.img_h == 0) return 0;

    std::vector<uint32_t> rgba(tim.img_w * tim.img_h, 0x00000000u);

    if (tim.has_clut && !tim.clut_pages.empty() && !tim.index_data.empty()) {
        int pi = std::clamp(clut_page_idx, 0, (int)tim.clut_pages.size() - 1);
        const ClutPage& pg = tim.clut_pages[pi];
        for (int i = 0; i < tim.img_w * tim.img_h; i++) {
            uint8_t idx = tim.index_data[i];
            rgba[i] = (idx < pg.colors) ? pg.palette[idx] : 0u;
        }
    } else if (!tim.direct_rgba.empty()) {
        rgba = tim.direct_rgba;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tim.img_w, tim.img_h,
        0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}