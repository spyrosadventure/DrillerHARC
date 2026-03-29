#pragma once

#include "imgui.h"
#include <glad/glad.h>
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>
#include <iostream>

#define EDIT_BLOCK_B    0x00
#define EDIT_BLOCK_G    0x01
#define EDIT_BLOCK_R    0x02
#define EDIT_BLOCK_Y    0x03
#define EDIT_STIFF      0x04
#define EDIT_IRON       0x05
#define EDIT_CRYSTAL05  0x06
#define EDIT_CRYSTAL10  0x07
#define EDIT_CRYSTAL15  0x08
#define EDIT_CRYSTAL20  0x09
#define EDIT_CRYSTAL25  0x0A
#define EDIT_CRYSTAL30  0x0B
#define EDIT_CRYSTAL35  0x0C
#define EDIT_CRYSTAL40  0x0D
#define EDIT_BLANK0     0x0E
#define EDIT_BLANK1     0x0F
#define EDIT_TIMER05    0x10
#define EDIT_TIMER10    0x11
#define EDIT_TIMER20    0x12
#define EDIT_TIMER30    0x13
#define EDIT_TIMER40    0x14
#define EDIT_TURN       0x15
#define EDIT_FLIP       0x16
#define EDIT_CRYSTAL    0x17
#define EDIT_AIR        0x18
#define EDIT_BOUNDARY   0x19

struct BlockInfo {
    const char* name;
    uint32_t    fallback_color; // used when no sprite is available
};

inline const BlockInfo& block_info(uint8_t id) {
    static const BlockInfo table[] = {
        { "BLOCK_B (Blue)",     IM_COL32( 80, 120, 220, 255) },
        { "BLOCK_G (Green)",    IM_COL32( 80, 200,  80, 255) },
        { "BLOCK_R (Red)",      IM_COL32(220,  60,  60, 255) },
        { "BLOCK_Y (Yellow)",   IM_COL32(220, 200,  50, 255) },
        { "STIFF (Stone)",      IM_COL32(150, 140, 130, 255) },
        { "IRON",               IM_COL32(100, 110, 120, 255) },
        { "CRYSTAL 0.5s",       IM_COL32(160, 220, 255, 255) },
        { "CRYSTAL 1.0s",       IM_COL32(140, 200, 255, 255) },
        { "CRYSTAL 1.5s",       IM_COL32(120, 180, 255, 255) },
        { "CRYSTAL 2.0s",       IM_COL32(100, 160, 255, 255) },
        { "CRYSTAL 2.5s",       IM_COL32( 80, 140, 240, 255) },
        { "CRYSTAL 3.0s",       IM_COL32( 60, 120, 220, 255) },
        { "CRYSTAL 3.5s",       IM_COL32( 40, 100, 200, 255) },
        { "CRYSTAL 4.0s",       IM_COL32( 20,  80, 180, 255) },
        { "BLANK",              IM_COL32( 20,  20,  20, 255) },
        { "BLANK",              IM_COL32( 20,  20,  20, 255) },
        { "TIMER +0.5s",        IM_COL32(255, 180,  80, 255) },
        { "TIMER +1.0s",        IM_COL32(255, 160,  60, 255) },
        { "TIMER +2.0s",        IM_COL32(255, 140,  40, 255) },
        { "TIMER +3.0s",        IM_COL32(255, 120,  20, 255) },
        { "TIMER +4.0s",        IM_COL32(255, 100,   0, 255) },
        { "TURN",               IM_COL32(180,  80, 220, 255) },
        { "FLIP",               IM_COL32(220,  80, 180, 255) },
        { "CRYSTAL (generic)",  IM_COL32(180, 230, 255, 255) },
        { "AIR (Oxygen)",       IM_COL32(200, 240, 200, 255) },
        { "BOUNDARY",           IM_COL32( 60,  60,  60, 255) },
    };
    static const BlockInfo unknown = { "UNKNOWN", IM_COL32(80, 80, 80, 255) };
    if (id >= sizeof(table) / sizeof(table[0])) return unknown;
    return table[id];
}

struct CourseViewer {
    bool                 open  = false;
    std::string          title;
    std::vector<uint8_t> data;

    // GL textures for the 4 colour blocks: 0=Blue 1=Green 2=Red 3=Yellow

    static constexpr int TIMER_COUNT = 5;
    GLuint timer_tex[TIMER_COUNT] = { 0, 0, 0, 0, 0 };
    bool   timer_sprites_loaded   = false;

    GLuint block_tex[4] = { 0, 0, 0, 0 };
    GLuint iron_tex = 0;
    GLuint crystal_tex = 0;
    GLuint stone_tex = 0;
    bool   sprites_loaded = false;
    bool   iron_sprites_loaded = false;
    bool   crystal_sprites_loaded = false;
    bool   stone_sprites_loaded = false;

    static constexpr int   COLS    = 9;
    static constexpr int   SPRITE  = 40;   // sprite size in the sheet
    static constexpr float CELL    = 40.f; // rendered cell size
    static constexpr float GAP     =  2.f;
    static constexpr float STEP    = CELL + GAP;

    void load_timer_sprites(const TimDecoded& tim) {
        free_timer_sprites();

        if (tim.img_w < SPRITE * TIMER_COUNT || tim.img_h < SPRITE) return;
        if (tim.index_data.empty() || tim.clut_pages.empty()) return;

        const ClutPage& pg = tim.clut_pages[0];

        for (int i = 0; i < TIMER_COUNT; i++) {
            int x_off = i * SPRITE; // each sprite is 40 pixels wide

            std::vector<uint32_t> sprite(SPRITE * SPRITE);
            for (int y = 0; y < SPRITE; y++) {
                for (int x = 0; x < SPRITE; x++) {
                    uint8_t idx = tim.index_data[y * tim.img_w + x_off + x];
                    sprite[y * SPRITE + x] = (idx < pg.colors) ? pg.palette[idx] : 0u;
                }
            }

            GLuint tex_id;
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE, SPRITE, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, sprite.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            timer_tex[i] = tex_id;
        }

        timer_sprites_loaded = true;
    }

    void free_timer_sprites() {
        for (int i = 0; i < TIMER_COUNT; i++) {
            if (timer_tex[i]) { glDeleteTextures(1, &timer_tex[i]); timer_tex[i] = 0; }
        }
        timer_sprites_loaded = false;
    }

    void load_block_sprites(const TimDecoded& tim) {
        free_sprites();

        if (tim.img_w < SPRITE || tim.img_h < SPRITE) return;
        if (tim.index_data.empty()) return;

        int pages = (int)tim.clut_pages.size();

        // Load textures for the colored blocks (Blue, Green, Red, Yellow)
        for (int page = 0; page < 4 && page < pages; page++) {
            const ClutPage& pg = tim.clut_pages[page];

            std::vector<uint32_t> sprite(SPRITE * SPRITE);
            for (int y = 0; y < SPRITE; y++) {
                for (int x = 0; x < SPRITE; x++) {
                    uint8_t idx = tim.index_data[y * tim.img_w + x];
                    sprite[y * SPRITE + x] = (idx < pg.colors) ? pg.palette[idx] : 0u;
                }
            }

            GLuint id;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE, SPRITE, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            block_tex[page] = id;
        }

        sprites_loaded = true;
    }

    void load_crystal_sprites(const TimDecoded& tim) {

        if (tim.img_w < SPRITE || tim.img_h < SPRITE) return;
        if (tim.index_data.empty()) return;

        int pages = (int)tim.clut_pages.size();

        // Load iron sprite (assuming iron is in the first page of its TIM)
        if (pages > 0) {
            const ClutPage& pg = tim.clut_pages[0]; // Use the first CLUT page

            std::vector<uint32_t> sprite(SPRITE * SPRITE);
            for (int y = 0; y < SPRITE; y++) {
                for (int x = 0; x < SPRITE; x++) {
                    uint8_t idx = tim.index_data[y * tim.img_w + x];
                    sprite[y * SPRITE + x] = (idx < pg.colors) ? pg.palette[idx] : 0u;
                }
            }

            GLuint tex_id;
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE, SPRITE, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            crystal_tex = tex_id;
        }

        crystal_sprites_loaded = true;
    }

    void load_iron_sprites(const TimDecoded& tim) {

        if (tim.img_w < SPRITE || tim.img_h < SPRITE) return;
        if (tim.index_data.empty()) return;

        int pages = (int)tim.clut_pages.size();

        // Load iron sprite
        if (pages > 0) {
            const ClutPage& pg = tim.clut_pages[0]; // Use the first CLUT page

            std::vector<uint32_t> sprite(SPRITE * SPRITE);
            for (int y = 0; y < SPRITE; y++) {
                for (int x = 0; x < SPRITE; x++) {
                    uint8_t idx = tim.index_data[y * tim.img_w + x];
                    sprite[y * SPRITE + x] = (idx < pg.colors) ? pg.palette[idx] : 0u;
                }
            }

            GLuint tex_id;
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE, SPRITE, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            iron_tex = tex_id;
        }

        iron_sprites_loaded = true;
    }
    void load_stone_sprites(const TimDecoded& tim) {

        if (tim.img_w < SPRITE || tim.img_h < SPRITE) return;
        if (tim.index_data.empty()) return;

        int pages = (int)tim.clut_pages.size();

        // Load stone sprite
        if (pages > 0) {
            const ClutPage& pg = tim.clut_pages[0]; // Use the first CLUT page

            std::vector<uint32_t> sprite(SPRITE * SPRITE);
            for (int y = 0; y < SPRITE; y++) {
                for (int x = 0; x < SPRITE; x++) {
                    uint8_t idx = tim.index_data[y * tim.img_w + x];
                    sprite[y * SPRITE + x] = (idx < pg.colors) ? pg.palette[idx] : 0u;
                }
            }

            GLuint tex_id;
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE, SPRITE, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            stone_tex = tex_id;
        }

        stone_sprites_loaded = true;
    }

    void free_sprites() {
        for (int i = 0; i < 4; i++) {
            if (block_tex[i]) { glDeleteTextures(1, &block_tex[i]); block_tex[i] = 0; }
        }
        sprites_loaded = false;
    }

    void load(const std::string& entry_name, const std::vector<uint8_t>& raw) {
        title = "Course: " + entry_name;
        data  = raw;
        open  = true;
    }

    bool draw() {
        if (!open) return false;

        float win_w = COLS * STEP + 32.f;
        ImGui::SetNextWindowSize({ win_w, 600.f }, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &open)) {
            ImGui::End();
            return open;
        }

        int rows = (int)data.size() / COLS;

        ImDrawList* dl    = ImGui::GetWindowDrawList();
        ImVec2      orig  = ImGui::GetCursorScreenPos();
        ImVec2      mouse = ImGui::GetMousePos();

        // Single invisible button covering the grid so the child scrolls / focuses correctly
        ImGui::InvisibleButton("##grid", { COLS * STEP, rows * STEP });

        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < COLS; col++) {
                int idx = row * COLS + col;
                if (idx >= (int)data.size()) break;

                uint8_t          raw_byte = data[idx];
                uint8_t          type     = raw_byte & 0x1F;
                const BlockInfo& bi       = block_info(type);

                ImVec2 tl = { orig.x + col * STEP, orig.y + row * STEP };
                ImVec2 br = { tl.x + CELL, tl.y + CELL };

                // Draw sprite for main blocks, flat colour for everything else
                bool drew_sprite = false;
                if (type <= EDIT_BLOCK_Y) {
                    GLuint tex = block_tex[type]; // 0=B,1=G,2=R,3=Y
                    if (tex) {
                        dl->AddImage((ImTextureID)(uintptr_t)tex, tl, br);
                        drew_sprite = true;
                    }
                    else {
                        std::cout << "No tex for block " << type;
                    }
                }
                if (type == EDIT_IRON) {
                    GLuint tex = iron_tex; 
                    if (tex) {
                        dl->AddImage((ImTextureID)(uintptr_t)tex, tl, br);
                        drew_sprite = true;
                    }
                }
                if (type >= EDIT_CRYSTAL05 && type <= EDIT_CRYSTAL40 or type == EDIT_CRYSTAL) {
                    GLuint tex = crystal_tex;
                    if (tex) {
                        dl->AddImage((ImTextureID)(uintptr_t)tex, tl, br);
                        drew_sprite = true;
                    }
                }
                if (type >= EDIT_TIMER05 && type <= EDIT_TIMER40) {
                    int i = type - EDIT_TIMER05; // 0..4
                    GLuint tex = timer_tex[i];
                    if (tex) {
                        dl->AddImage((ImTextureID)(uintptr_t)tex, tl, br);
                        drew_sprite = true;
                    }
                }
                if (type == EDIT_STIFF) {
                    GLuint tex = stone_tex; 
                    if (tex) {
                        dl->AddImage((ImTextureID)(uintptr_t)tex, tl, br);
                        drew_sprite = true;
                    }
                }
                if (!drew_sprite) {
                    dl->AddRectFilled(tl, br, bi.fallback_color, 3.f);
                }

                // Hover highlight + tooltip
                bool hovered = mouse.x >= tl.x && mouse.x < br.x
                    && mouse.y >= tl.y && mouse.y < br.y;
                if (hovered) {
                    dl->AddRect(tl, br, IM_COL32(255, 255, 255, 200), 3.f, 0, 2.f);
                    ImGui::SetTooltip("[%02X]  row %d  col %d\n%s", raw_byte, row, col, bi.name);
                }
            }
        }

        ImGui::End();
        return open;
    }
};
