#include "saturn_rom_extract.h"
#include "saturn_assets.h"
#include "saturn.h"

#include <filesystem>
#include <utility>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>

#include "saturn/libs/portable-file-dialogs.h"

extern "C" {
#include "pc/pc_main.h"
#ifdef FILE_PICKER
#include "pc/gtk/filepicker.h"
#endif
#include "pc/platform.h"
#include "pc/pngutils.h"
#include "pc/cliopts.h"
#include "pc/gfx/gfx_pc.h"
}

std::string currently_extracting = "";

enum FormatEnum {
    FMT_RGBA,
    FMT_IA,
    FMT_I,
};

struct FormatTableEntry {
    FormatEnum format;
    int depth;
};

std::map<std::string, FormatTableEntry> format_table = {
    { "rgba16", (FormatTableEntry){ FMT_RGBA, 16 } },
    { "rgba32", (FormatTableEntry){ FMT_RGBA, 32 } },
    { "ia1",    (FormatTableEntry){ FMT_IA,   1  } },
    { "ia4",    (FormatTableEntry){ FMT_IA,   4  } },
    { "ia8",    (FormatTableEntry){ FMT_IA,   8  } },
    { "ia16",   (FormatTableEntry){ FMT_IA,   16 } },
    { "i4",     (FormatTableEntry){ FMT_I,    4  } },
    { "i8",     (FormatTableEntry){ FMT_I,    8  } }
};

#define EXTRACT_PATH std::filesystem::path(sys_user_path()) / "res"

struct mio0_header {
    unsigned int dest_size;
    unsigned int comp_offset;
    unsigned int uncomp_offset;
};

mio0_header read_mio0_header(unsigned char* data) {
    int ptr = 4;
    mio0_header header;
#define READ_U32 (data[ptr++] << 24) | (data[ptr++] << 16) | (data[ptr++] << 8) | data[ptr++]
    header.dest_size = READ_U32;
    header.comp_offset = READ_U32;
    header.uncomp_offset = READ_U32;
#undef READ_U32
    return header;
}

unsigned char* read_mio0(unsigned char* buf) {
    int bytes_written = 0;
    int bit_idx = 0;
    int comp_idx = 0;
    int uncomp_idx = 0;
    mio0_header head = read_mio0_header(buf);
    unsigned char* out = (unsigned char*)malloc(head.dest_size);
    while (bytes_written < head.dest_size) {
        if ((&buf[16])[bit_idx / 8] & (1 << (7 - (bit_idx % 8)))) {
            out[bytes_written] = buf[head.uncomp_offset + uncomp_idx];
            bytes_written++;
            uncomp_idx++;
        }
        else {
            int idx;
            int length;
            int i;
            unsigned char* vals = &buf[head.comp_offset + comp_idx];
            comp_idx += 2;
            length = ((vals[0] & 0xF0) >> 4) + 3;
            idx = ((vals[0] & 0x0F) << 8) + vals[1] + 1;
            for (i = 0; i < length; i++) {
                out[bytes_written] = out[bytes_written - idx];
                bytes_written++;
            }
        }
        bit_idx++;
    }
    return out;
}

struct Range {
      signed char off;
    unsigned char len;
};

#define BIT(arr, bit) (((arr)[(bit) / 8] >> (7 - (bit) % 8)) & 1)

void decode_image(unsigned char* img, unsigned char* raw, int pixels, std::vector<Range> ranges) {
    int bitptr = 0;
    int ptr = 0;
    for (int i = 0; i < pixels; i++) {
        int len = 0;
        for (const auto& range : ranges) {
            if (range.off == -1) {
                img[ptr++] = range.len;
                continue;
            }
            int end = range.off + range.len;
            if (len < end) len = end;
            int value = 0;
            for (int j = 0; j < range.len; j++) {
                value = (value << 1) | BIT(raw, bitptr + range.off + j);
            }
            img[ptr++] = value * 0xFF / ((1 << range.len) - 1);
        }
        bitptr += len;
    }
}

#undef BIT

#define RANGE(_off, _len) (Range){ .off = _off, .len = _len  }
#define CONST(value)      (Range){ .off = -1  , .len = value }

unsigned char* raw2rgba(unsigned char* raw, int width, int height, int depth) {
    unsigned char* img = (unsigned char*)malloc(width * height * 4);
    switch (depth) {
        case 16:
            decode_image(img, raw, width * height, {
                RANGE(0 , 5),
                RANGE(5 , 5),
                RANGE(10, 5),
                RANGE(15, 1)
            });
            break;
        case 32:
            decode_image(img, raw, width * height, {
                RANGE(0 , 8),
                RANGE(8 , 8),
                RANGE(16, 8),
                RANGE(24, 8)
            });
            break;
    }
    return img;
}

unsigned char* raw2ia(unsigned char* raw, int width, int height, int depth) {
    unsigned char* img = (unsigned char*)malloc(width * height * 2);
    switch (depth) {
        case 1:
            decode_image(img, raw, width * height, {
                RANGE(0, 1),
                RANGE(0, 1)
            });
            break;
        case 4:
            decode_image(img, raw, width * height, {
                RANGE(0, 3),
                RANGE(3, 1)
            });
            break;
        case 8:
            decode_image(img, raw, width * height, {
                RANGE(0, 4),
                RANGE(4, 4)
            });
            break;
        case 16:
            decode_image(img, raw, width * height, {
                RANGE(0, 8),
                RANGE(8, 8)
            });
            break;
    }
    return img;
}

unsigned char* raw2i(unsigned char* raw, int width, int height, int depth) {
    unsigned char* img = (unsigned char*)malloc(width * height * 2);
    switch (depth) {
        case 4:
            decode_image(img, raw, width * height, {
                RANGE(0, 4),
                CONST(255),
            });
            break;
        case 8:
            decode_image(img, raw, width * height, {
                RANGE(0, 8),
                CONST(255),
            });
            break;
    }
    return img;
}

unsigned char* raw2skybox(unsigned char* raw, int len, int use_bitfs) {
    int table_index = len - 8 * 10 * 4;
    unsigned int table[80];
    for (int i = 0; i < 80; i++) {
        table[i] = (raw[table_index + i * 4 + 0] << 24) |
                   (raw[table_index + i * 4 + 1] << 16) |
                   (raw[table_index + i * 4 + 2] <<  8) |
                   (raw[table_index + i * 4 + 3] <<  0);
    }
    unsigned char* skybox = (unsigned char*)malloc(80 * 32 * 32 * 4);
    unsigned int base = table[0];
    for (int i = 0; i < 80; i++) {
        // for some reason the bitfs ptr table is completely fucked
        table[i] = use_bitfs ? bitfs_ptrtable[i] * 0x800 : table[i] - base;
    }
    for (int i = 0; i < 80; i++) {
        decode_image(skybox + i * 32 * 32 * 4, raw + table[i], 32 * 32, {
            RANGE(0 , 5),
            RANGE(5 , 5),
            RANGE(10, 5),
            RANGE(15, 1)
        });
    }
    return skybox;
}

#undef RANGE
#undef CONST

void write_png(std::string path, void* data, int width, int height, int depth) {
    std::filesystem::path dst = EXTRACT_PATH / path;
    std::filesystem::create_directories(dst.parent_path());
    std::cout << "exporting " << dst << std::endl;
    pngutils_write_png(std::filesystem::absolute(dst).u8string().c_str(), width, height, depth, data, 0);
}

void skybox2png(std::string filename, unsigned char* img) {
    for (int i = 0; i < 80; i++) {
        write_png(filename + "." + std::to_string(i) + ".rgba16.png", img + i * 32 * 32 * 4, 32, 32, 4);
    }
    free(img);
}

void rgba2png(std::string filename, unsigned char* img, int width, int height) {
    write_png(filename, img, width, height, 4);
    free(img);
}

void ia2png(std::string filename, unsigned char* img, int width, int height) {
    write_png(filename, img, width, height, 2);
    free(img);
}

unsigned char* file_processor_apply(unsigned char* data, std::pair<int, unsigned int*> file_processor) {
    unsigned char* processed = (unsigned char*)malloc(file_processor.first);
    for (int i = 0; i < file_processor.first; i++) {
        processed[i] = data[file_processor.second[i]];
    }
    return processed;
}

#define ROM_OK           0
#define ROM_NEED_EXTRACT 1
#define ROM_MISSING      2
#define ROM_INVALID      3

#define TEXTYPE_OTHER      0
#define TEXTYPE_FONT       1
#define TEXTYPE_TRANSITION 2

int saturn_rom_status(std::filesystem::path extract_dest, std::vector<std::string>* todo, int type) {
    bool needs_extract = false;
    bool needs_rom = false;
    for (const auto& entry : assets) {
        if ((entry.metadata.size() == 0) && !(type & EXTRACT_TYPE_SOUND)) continue;
        int textype = TEXTYPE_OTHER;
        if (std::find(menu_font.begin(), menu_font.end(), entry.path) != menu_font.end()) textype = TEXTYPE_FONT;
        if (entry.path.find("segment2.0F458.ia8") != std::string::npos ||
            entry.path.find("segment2.0FC58.ia8") != std::string::npos) textype = TEXTYPE_TRANSITION;
        if (textype == TEXTYPE_OTHER      && (entry.metadata.size() != 0) && !(type & EXTRACT_TYPE_TEXTURES  )) continue;
        if (textype == TEXTYPE_FONT       && (entry.metadata.size() != 0) && !(type & EXTRACT_TYPE_FONT      )) continue;
        if (textype == TEXTYPE_TRANSITION && (entry.metadata.size() != 0) && !(type & EXTRACT_TYPE_TRANSITION)) continue;
        if (entry.metadata.size() > 0 && entry.metadata[0] == -1) {
            bool missing = false;
            std::filesystem::path path = extract_dest / entry.path;
            std::string filename = path.string();
            std::string ext = path.extension().string();
            std::string name_without_ext = filename.substr(0, filename.length() - ext.length());
            for (int i = 0; i < 80; i++) {
                if (!std::filesystem::exists(name_without_ext + "." + std::to_string(i) + ".rgba16.png")) missing = true;
            }
            if (missing) {
                needs_extract = true;
                if (todo != nullptr) todo->push_back(entry.path);
            }
            continue;
        }
        if (!std::filesystem::exists(extract_dest / entry.path)) {
            needs_extract = true;
            if (todo != nullptr) todo->push_back(entry.path);
        }
    }
    needs_rom = needs_extract;
    if (type & EXTRACT_TYPE_SATURN) {
        for (const auto& entry : saturn_assets) {
            if (!std::filesystem::exists(extract_dest / entry.path)) {
                needs_extract = true;
                if (todo != nullptr) todo->push_back(entry.path);
            }
        }
    }
    if (!needs_extract) return ROM_OK;
    if (needs_rom && !std::filesystem::exists(gCLIOpts.RomPath)) {
#ifdef FILE_PICKER
        open_file_picker();
#endif
        if (!std::filesystem::exists(gCLIOpts.RomPath)) return ROM_MISSING;
    }
    return ROM_NEED_EXTRACT;
}

// AppImage AppDir
// copy external files from out of read-only AppImage to read-write user folder, 
// usually ~/.local/share/v64saturn
int copy_custom_assets(void) {
    // AppImage environment variable
    const char *appdir_cstr = std::getenv("APPDIR");
    const char *exe_path_cstr = sys_exe_path();
    const char *user_path_cstr = sys_user_path();

    if ((exe_path_cstr == NULL && appdir_cstr == NULL) || user_path_cstr == NULL) {

        pfd::message("Failed to copy custom assets", "Failed to locate requred directories!");
        return 4;
    }

    std::string srcdir, destdir = std::string(user_path_cstr);

    // This affects non-AppImage builds also which means that if I leave it this way,
    // all AppImage support might need to be toggled by a condition.
    if (appdir_cstr == NULL) {
        srcdir = std::string(exe_path_cstr);
    }
    else {
        srcdir = std::string(appdir_cstr) + "/custom_assets/";
    }
    if (!std::filesystem::exists(srcdir)) {
        pfd::message("Failed to copy custom assets", "Failed to detect required custom asset files for Saturn!");
        return 5;
    }
    if (srcdir == destdir) {
        return 0;
    }

    // https://stackoverflow.com/a/51431426/11708026
    // This copies way more than necessary, but the extra files don't break anything,
    // and when I once hardcoded a whitelist of files to copy out of an Android .apk,
    // eventually I had to edit it to add more names, and others actually became 
    // confused when they tried to add files to the build and couldn't find the whitelist.
    try {
        std::filesystem::copy(std::filesystem::path(srcdir),
                              std::filesystem::path(sys_user_path()),
                              std::filesystem::copy_options::overwrite_existing |
                              std::filesystem::copy_options::recursive);
    }
    catch (std::exception& e) {
        pfd::message("Failed to copy custom assets", "Failed to copy required custom asset files for Saturn!");
        std::cout << e.what();
        return 6;
    }

    return 0;
}

int saturn_extract_rom(int type) {
    std::filesystem::path extract_dest = EXTRACT_PATH;
    std::vector<std::string> todo = {};
    int status = saturn_rom_status(extract_dest, &todo, type);

    if (status == ROM_OK) return ROM_OK;
    if (status == ROM_MISSING) {
        pfd::message("Missing ROM", "Cannot find sm64.z64\n\nPlease select your unmodified US Super Mario 64 z64 ROM by placing \"sm64.z64\" in the current working directory, using the --rom argument, or using the GTK file picker.", pfd::choice::ok);
        return ROM_MISSING;
    }
    if (status == ROM_INVALID) {
        pfd::message("Invalid ROM", "Couldn't verify sm64.z64\n\nThe file may be corrupted, extended, or from the wrong region. Use an unmodified US version of SM64", pfd::choice::ok);
        return ROM_INVALID;
    }
    extraction_progress = 0;
    std::ifstream stream = std::ifstream(gCLIOpts.RomPath, std::ios::binary);
    unsigned char* data = (unsigned char*)malloc(1024 * 1024 * 8);
    stream.read((char*)data, 1024 * 1024 * 8);
    stream.close();
    std::map<int, unsigned char*> mio0 = {};
    for (const auto& asset : assets) {
        if (std::find(todo.begin(), todo.end(), asset.path) == todo.end()) continue;
        if (mio0.find(asset.mio0) != mio0.end()) continue;
        if (asset.mio0 == -1) mio0.insert({ -1, data });
        else mio0.insert({ asset.mio0, read_mio0(data + asset.mio0) });
    }
    int count = 0;
    for (const auto& asset : assets) {
        extraction_progress = count++ / (float)assets.size();
        if (std::find(todo.begin(), todo.end(), asset.path) == todo.end()) continue;
        currently_extracting = asset.path;
        std::istringstream iss = std::istringstream(asset.path);
        std::vector<std::string> tokens = {};
        std::string token;
        while (std::getline(iss, token, '.')) {
            tokens.push_back(token);
        }
        unsigned char* buf = mio0[asset.mio0];
        if (tokens[tokens.size() - 1] == "png") {
            if (asset.metadata[0] == -1) {
                unsigned char* img = raw2skybox(buf + asset.pos, asset.len, asset.path == "gfx/textures/skyboxes/bitfs.png");
                skybox2png(tokens[0], img);
                continue;
            }
            FormatTableEntry format = format_table[tokens[tokens.size() - 2]];
            if (format.format == FMT_RGBA) {
                unsigned char* img = raw2rgba(buf + asset.pos, asset.metadata[0], asset.metadata[1], format.depth);
                rgba2png(asset.path, img, asset.metadata[0], asset.metadata[1]);
            }
            else if (format.format == FMT_IA) {
                unsigned char* img = raw2ia(buf + asset.pos, asset.metadata[0], asset.metadata[1], format.depth);
                ia2png(asset.path, img, asset.metadata[0], asset.metadata[1]);
            }
            else if (format.format == FMT_I) {
                unsigned char* img = raw2i(buf + asset.pos, asset.metadata[0], asset.metadata[1], format.depth);
                ia2png(asset.path, img, asset.metadata[0], asset.metadata[1]);
            }
        }
        else {
            std::filesystem::path dst = EXTRACT_PATH / asset.path;
            std::filesystem::create_directories(dst.parent_path());
            std::cout << "exporting " << dst << std::endl;
            unsigned char* out_data;
            int data_len;
            bool is_processed = false;
            if (file_processor.find(asset.path) == file_processor.end()) {
                out_data = buf + asset.pos;
                data_len = asset.len;
            }
            else {
                out_data = file_processor_apply(buf + asset.pos, file_processor[asset.path]);
                data_len = file_processor[asset.path].first;
                is_processed = true;
            }
            std::ofstream out = std::ofstream(dst, std::ios::binary);
            out.write((char*)out_data, data_len);
            out.close();
            if (is_processed) free(out_data);
        }
    }
    for (const auto& asset : saturn_assets) {
        if (std::find(todo.begin(), todo.end(), asset.path) == todo.end()) continue;
        currently_extracting = asset.path;
        unsigned char* img = raw2rgba(asset.data, asset.metadata[0], asset.metadata[1], 16);
        rgba2png(asset.path, img, asset.metadata[0], asset.metadata[1]);
    }
    for (const auto& entry : mio0) {
        free(entry.second);
    }
    extraction_progress = 1;
    std::cout << "extraction finished" << std::endl;
    gfx_precache_textures();
    return ROM_OK;
}
