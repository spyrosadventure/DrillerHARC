#define _CRT_SECURE_NO_WARNINGS 1

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <zlib.h>
#include "TIMDecoder.h"

#include "MrDrillerROM.h"     

#include "CourseViewer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif
#include <iostream>

#include <algorithm>
#include <cctype>

bool ci_equal(const std::string& str1, const std::string& str2) {
    if (str1.size() != str2.size()) return false;
    return std::equal(str1.begin(), str1.end(), str2.begin(), 
        [](char c1, char c2) { return tolower(c1) == tolower(c2); });
}

#pragma pack(push, 1)
struct HarcHeader {
    char magic[4];
    uint32_t version_flags;
    uint32_t entry_table_offset;
    uint32_t entry_count;
};
struct HarcEntryRaw {
    uint32_t data_offset;
    uint32_t comp_size;
    uint32_t raw_size;
    uint32_t parent;
    uint32_t flags_a;
    uint32_t flags_b;
    char name[24];
};
#pragma pack(pop)

static constexpr uint32_t NO_PARENT = 0xFFFFFFFFu;

struct HarcEntry {
    int idx = 0;
    uint32_t data_offset  = 0;
    uint32_t comp_size = 0;
    uint32_t raw_size = 0;
    uint32_t parent = NO_PARENT;
    uint32_t flags_a = 0;
    uint32_t flags_b = 0;
    char name[25] = {};
    bool is_root = false;
    bool is_dir = false;
    bool is_file = false;
    std::vector<int> children;
};

struct Archive {
    std::vector<HarcEntry> entries;
    int root_idx = -1;
    FILE* fh = nullptr;
};

static std::vector<uint8_t> read_entry(Archive& arc, const HarcEntry& e) {
    if (!e.is_file) return {};
    fprintf(stderr, "reading entry '%s': offset=0x%X comp=%u raw=%u\n",
        e.name, e.data_offset, e.comp_size, e.raw_size);
    fseek(arc.fh, (long)e.data_offset, SEEK_SET);
    std::vector<uint8_t> buf(e.comp_size);
    if (fread(buf.data(), 1, e.comp_size, arc.fh) != e.comp_size) {
        fprintf(stderr, "  fread failed\n");
        return {};
    }
    fprintf(stderr, "  first bytes: %02X %02X %02X %02X\n",
        buf[0], buf[1], buf[2], buf[3]);
    if (e.comp_size == e.raw_size) return buf;
    auto out = z_decompress(buf.data(), e.comp_size);
    if (out.empty()) fprintf(stderr, "  decompress failed\n");
    return out;
}

static bool load_harc(const char* path, Archive& arc) {
    arc.fh = fopen(path, "rb");
    if (!arc.fh) return false;

    HarcHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, arc.fh) != 1) return false;
    if (memcmp(hdr.magic, "HARC", 4) != 0) return false;

    fseek(arc.fh, 0, SEEK_END);
    long file_size = ftell(arc.fh);
    fseek(arc.fh, (long)hdr.entry_table_offset, SEEK_SET);

    for (uint32_t i = 0; i < hdr.entry_count; i++) {
        HarcEntryRaw raw;
        if (fread(&raw, sizeof(raw), 1, arc.fh) != 1) break;

        uint32_t par = raw.parent;
        if (par != NO_PARENT && par >= hdr.entry_count) break;
        if (raw.comp_size > (uint32_t)file_size)        break;

        HarcEntry e{};
        e.idx         = (int)i;
        e.data_offset = raw.data_offset;
        e.comp_size   = raw.comp_size;
        e.raw_size    = raw.raw_size;
        e.parent      = par;
        e.flags_a     = raw.flags_a;
        e.flags_b     = raw.flags_b;
        memcpy(e.name, raw.name, 24);
        e.name[24]    = '\0';
        e.is_root     = (par == NO_PARENT);
        e.is_dir      = !e.is_root && (raw.comp_size == 0 && raw.raw_size == 0 && raw.data_offset == 0);
        e.is_file     = !e.is_root && !e.is_dir;
        arc.entries.push_back(e);
    }

    if (arc.entries.empty()) return false;

    for (auto& e : arc.entries) {
        if (e.is_root) { arc.root_idx = e.idx; continue; }
        if (e.parent < arc.entries.size())
            arc.entries[e.parent].children.push_back(e.idx);
    }
    return arc.root_idx >= 0;
}

static std::string save_dialog(const char* filter, const char* default_ext) {
    char path[MAX_PATH] = {};
#ifdef _WIN32
    OPENFILENAMEA ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = filter;   

    ofn.lpstrFile       = path;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrDefExt     = default_ext;
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) return {};
#endif
    return path;
}

static std::string open_dialog(const char* filter) {
    char path[MAX_PATH] = {};
#ifdef _WIN32
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return {};
#endif
    return path;
}

static bool has_ext(const std::string& name, const char* ext) {

    if (name.size() < strlen(ext)) return false;
    std::string tail = name.substr(name.size() - strlen(ext));
    std::transform(tail.begin(), tail.end(), tail.begin(), ::toupper);
    return tail == ext;
}

enum class ArchiveKind { None, HARC, ROM };

struct AppState {
    // good styling for this fucking mess of a struct :)
    ArchiveKind arc_kind = ArchiveKind::None;

    Archive    arc;

    RomArchive rom;
    int        rom_selected_idx = -1;   

    int        selected_idx  = -1;      

    TimDecoded tim;
    GLuint     gl_tex        = 0;
    int        clut_page     = 0;
    float      zoom          = 1.0f;
    std::vector<uint8_t> raw_data;
    std::string status;
    CourseViewer course_viewer;

    void free_tex() {
        if (gl_tex) { glDeleteTextures(1, &gl_tex); gl_tex = 0; }
    }

    void rebuild_tex() {
        free_tex();
        gl_tex = build_gl_texture(tim, clut_page);
    }

    void decode_preview(const std::string& filename) {
        free_tex();
        tim       = {};
        zoom      = 1.0f;
        clut_page = 0;
        status    = "";

        if (raw_data.empty()) { status = "Failed to read entry"; return; }

        bool is_tim = has_ext(filename, ".TIM");
        bool is_dat = has_ext(filename, ".DAT");

        if (is_dat) {
            course_viewer.load(filename, raw_data);
            return;
        }
        if (!is_tim) return;  

        if (is_tim) {
            tim = decode_tim_data(raw_data.data(), raw_data.size());
        }

        if (tim.img_w == 0) {
            status = is_tim ? "Failed to decode TIM" : "Failed to decode TM2";
            return;
        }
        rebuild_tex();
    }

    void select_entry(int idx) {
        if (idx == selected_idx) return;
        selected_idx = idx;
        rom_selected_idx = -1;
        raw_data = {};

        if (idx < 0 || idx >= (int)arc.entries.size()) return;
        const HarcEntry& e = arc.entries[idx];
        if (!e.is_file) return;

        raw_data = read_entry(arc, e);
        decode_preview(e.name);
    }

    void select_rom_entry(int idx) {
        if (idx == rom_selected_idx) return;
        rom_selected_idx = idx;
        selected_idx = -1;
        raw_data = {};

        if (idx < 0 || idx >= (int)rom.entries.size()) return;
        const RomEntry& e = rom.entries[idx];

        raw_data = read_rom_entry(rom, e);
        decode_preview(e.name);
    }
};

static void draw_tree_node(AppState& app, int idx) {
    if (idx < 0 || idx >= (int)app.arc.entries.size()) return;
    const HarcEntry& e = app.arc.entries[idx];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (e.children.empty())        flags |= ImGuiTreeNodeFlags_Leaf;
    if (idx == app.selected_idx)   flags |= ImGuiTreeNodeFlags_Selected;

    char label[48];
    snprintf(label, sizeof(label), "%s%s", e.name, (e.is_root || e.is_dir) ? "/" : "");

    bool open = ImGui::TreeNodeEx((void*)(intptr_t)idx, flags, "%s", label);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        app.select_entry(idx);

    if (open) {
        for (int c : e.children) draw_tree_node(app, c);
        ImGui::TreePop();
    }
}

static void draw_rom_tree(AppState& app) {
    for (int i = 0; i < (int)app.rom.entries.size(); i++) {
        const RomEntry& e = app.rom.entries[i];

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_SpanFullWidth;
        if (i == app.rom_selected_idx)
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", e.name);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            app.select_rom_entry(i);
        ImGui::TreePop();
    }
}

static void draw_preview_panel(AppState& app, const char* entry_name,
    uint32_t offset, uint32_t size_on_disk, uint32_t raw_size) {
    ImGui::TextDisabled("[FILE]"); ImGui::SameLine();
    ImGui::Text("%s", entry_name);
    ImGui::Separator();

    ImGui::Text("Offset 0x%08X   On-disk %u B   Raw %u B", offset, size_on_disk, raw_size);

    if (!app.status.empty()) {
        ImGui::TextColored({1,.4f,.4f,1}, "%s", app.status.c_str());
    } else if (app.gl_tex) {
        ImGui::Text("BPP: %d   Size: %d x %d   CLUT: %s",
            app.tim.bpp, app.tim.img_w, app.tim.img_h,
            app.tim.has_clut ? "yes" : "no");

        if (app.tim.has_clut) {
            int num_pages = (int)app.tim.clut_pages.size();
            ImGui::SameLine(0, 20);
            ImGui::Text("Palette:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);

            bool changed = false;
            if (ImGui::SliderInt("##clut_page", &app.clut_page, 0, num_pages - 1)) {
                app.clut_page = std::clamp(app.clut_page, 0, num_pages - 1);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##prev", ImGuiDir_Left) && app.clut_page > 0) {
                app.clut_page--; changed = true;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##next", ImGuiDir_Right) && app.clut_page < num_pages - 1) {
                app.clut_page++; changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d / %d", app.clut_page + 1, num_pages);
            if (changed) app.rebuild_tex();

            const ClutPage& pg = app.tim.clut_pages[app.clut_page];
            ImGui::Spacing();
            float sw = 16.f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 sp = ImGui::GetCursorScreenPos();
            for (int c = 0; c < pg.colors; c++) {
                uint32_t col = pg.palette[c];
                uint8_t r = col & 0xFF, g = (col>>8)&0xFF, b=(col>>16)&0xFF, a=(col>>24)&0xFF;
                ImVec2 tl = { sp.x + c * sw, sp.y };
                ImVec2 br = { tl.x + sw,     tl.y + sw };
                dl->AddRectFilled(tl, br, IM_COL32(r,g,b,a));
                dl->AddRect(tl, br, IM_COL32(0,0,0,60));
            }
            ImGui::Dummy({pg.colors * sw, sw});
            ImGui::Spacing();
        }

        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Zoom", &app.zoom, 0.25f, 8.f, "%.2fx");

        ImGui::SameLine(0, 20);
        if (ImGui::Button("Export PNG")) {
            auto path = save_dialog("PNG Files\0*.png\0All Files\0*.*\0", "png");
            if (!path.empty()) {
                std::vector<uint32_t> rgba(app.tim.img_w * app.tim.img_h);
                if (app.tim.has_clut && !app.tim.clut_pages.empty()) {
                    int pi = std::clamp(app.clut_page, 0, (int)app.tim.clut_pages.size()-1);
                    const ClutPage& pg = app.tim.clut_pages[pi];
                    for (int i = 0; i < app.tim.img_w * app.tim.img_h; i++) {
                        uint8_t idx = app.tim.index_data[i];
                        rgba[i] = idx < pg.colors ? pg.palette[idx] : 0u;
                    }
                } else {
                    rgba = app.tim.direct_rgba;
                }
                int ok = stbi_write_png(path.c_str(),
                    app.tim.img_w, app.tim.img_h, 4,
                    rgba.data(), app.tim.img_w * 4);
                app.status = ok ? "Saved." : "PNG write failed.";
            }
        }

        ImGui::Separator();

        float dw = app.tim.img_w * app.zoom;
        float dh = app.tim.img_h * app.zoom;
        ImGui::BeginChild("##canvas", ImGui::GetContentRegionAvail(), false,
            ImGuiWindowFlags_HorizontalScrollbar);

        ImVec2 cur = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uintptr_t)app.gl_tex, {dw, dh});
        ImGui::GetWindowDrawList()->AddRect(
            cur,
            { cur.x + dw, cur.y + dh },
            IM_COL32(180, 180, 180, 255)
        );

        ImGui::EndChild();
    } else {
        if (ImGui::Button("Extract")) {
            auto path = save_dialog("All Files\0*.*\0", "");
            if (!path.empty()) {
                if (!app.raw_data.empty()) {
                    FILE* f = fopen(path.c_str(), "wb");
                    if (f) { fwrite(app.raw_data.data(), 1, app.raw_data.size(), f); fclose(f); app.status = "Extracted."; }
                    else app.status = "Failed to open output file.";
                } else {
                    app.status = "Failed to read entry.";
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: harc <archive.dat | archive.ROM>\n");
        return 1;
    }

    AppState app;

    if (load_harc(argv[1], app.arc)) {
        app.arc_kind = ArchiveKind::HARC;
        fprintf(stderr, "Opened as HARC  (%zu entries)\n", app.arc.entries.size());
    } else {

        if (app.arc.fh) { fclose(app.arc.fh); app.arc.fh = nullptr; }
        if (load_rom(argv[1], app.rom)) {
            app.arc_kind = ArchiveKind::ROM;
            fprintf(stderr, "Opened as Mr.Driller ROM  (%zu files)\n", app.rom.entries.size());
        } else {
            fprintf(stderr, "Failed to load '%s' as HARC or Mr.Driller ROM\n", argv[1]);
            return 1;
        }
    }

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(1280, 800, "HARC Viewer", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (app.arc_kind == ArchiveKind::HARC) {
        for (auto& e : app.arc.entries) {
            std::string nm = e.name;
            std::transform(nm.begin(), nm.end(), nm.begin(), ::toupper);

            if (nm == "BLK17M.TIM") {
                auto raw = read_entry(app.arc, e);
                if (!raw.empty()) {
                    auto tim = decode_tim_data(raw.data(), raw.size());
                    app.course_viewer.load_block_sprites(tim);
                }
            }
            if (nm == "BLKMTM.TIM") {
                auto raw = read_entry(app.arc, e);
                if (!raw.empty()) {
                    auto tim = decode_tim_data(raw.data(), raw.size());
                    app.course_viewer.load_iron_sprites(tim);
                }
            }
            if (nm == "BLOCK07M.TIM") {
                auto raw = read_entry(app.arc, e);
                if (!raw.empty()) {
                    auto tim = decode_tim_data(raw.data(), raw.size());
                    app.course_viewer.load_crystal_sprites(tim);
                }
            }
            if (nm == "TIMER40.TIM") {
                auto raw = read_entry(app.arc, e);
                if (!raw.empty()) {
                    auto tim = decode_tim_data(raw.data(), raw.size());
                    app.course_viewer.load_timer_sprites(tim);
                }
            }
            if (nm == "ROCK_K.TIM") {
                auto raw = read_entry(app.arc, e);
                if (!raw.empty()) {
                    auto tim = decode_tim_data(raw.data(), raw.size());
                    app.course_viewer.load_stone_sprites(tim);
                }
            }
        }
    }

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   {0,0});
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNav       | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar()) {
            const char* kind_label =
                app.arc_kind == ArchiveKind::HARC ? "HARC:" : "ROM:";
            ImGui::TextDisabled("%s", kind_label); ImGui::SameLine();
            ImGui::Text("%s", argv[1]);
            ImGui::EndMenuBar();
        }

        float avail_h = ImGui::GetContentRegionAvail().y;

        ImGui::BeginChild("##tree", {280, avail_h}, true);
        if (app.arc_kind == ArchiveKind::HARC && app.arc.root_idx >= 0)
            draw_tree_node(app, app.arc.root_idx);
        else if (app.arc_kind == ArchiveKind::ROM)
            draw_rom_tree(app);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##right", {0, avail_h}, false);

        if (app.arc_kind == ArchiveKind::HARC) {

            if (app.selected_idx >= 0 && app.selected_idx < (int)app.arc.entries.size()) {
                const HarcEntry& e = app.arc.entries[app.selected_idx];
                ImGui::TextDisabled("[%s]", e.is_root ? "ROOT" : e.is_dir ? "DIR" : "FILE");
                ImGui::SameLine(); ImGui::Text("%s", e.name);
                ImGui::Separator();

                if (e.is_file) {
                    draw_preview_panel(app, e.name, e.data_offset, e.comp_size, e.raw_size);
                } else {
                    ImGui::TextDisabled("Directory \x97 select a .TIM file to preview.");
                }
            } else {
                ImGui::TextDisabled("Select an entry from the tree.");
            }
        } else {

            if (app.rom_selected_idx >= 0 &&
                app.rom_selected_idx < (int)app.rom.entries.size()) {
                const RomEntry& e = app.rom.entries[app.rom_selected_idx];
                draw_preview_panel(app, e.name, e.offset, e.length, e.length);
            } else {
                ImGui::TextDisabled("Select a file from the list.");

                ImGui::Spacing();
                ImGui::TextDisabled("Archive : %s", app.rom.arc_name);
                ImGui::TextDisabled("ROM size: %u B  |  Files: %u",
                    app.rom.rom_size, app.rom.file_count);
            }
        }

        ImGui::EndChild();
        ImGui::End();

        app.course_viewer.draw();

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(.12f, .12f, .12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    app.free_tex();
    if (app.arc.fh)  fclose(app.arc.fh);
    if (app.rom.fh)  fclose(app.rom.fh);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}