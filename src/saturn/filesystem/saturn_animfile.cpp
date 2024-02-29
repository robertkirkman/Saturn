#include "saturn/filesystem/saturn_animfile.h"

#include "saturn/filesystem/saturn_format.h"
#include <vector>

extern "C" {
#include "pc/platform.h"
}

const int curr_ver = 1;

std::vector<int> favorite_anims = {};

void saturn_favorite_anim_data_handler(SaturnFormatStream* stream, int version) {
    int count = saturn_format_read_int32(stream);
    for (int i = 0; i < count; i++) {
        favorite_anims.push_back(saturn_format_read_int32(stream));
    }
}

void saturn_load_favorite_anims() {
    char anim_favorites_path[SYS_MAX_PATH] = "";
    strncat(anim_favorites_path,  sys_user_path(), SYS_MAX_PATH);
    strncat(anim_favorites_path, "/dynos/anim_favorites.bin", SYS_MAX_PATH);

    favorite_anims.clear();
    saturn_format_input(anim_favorites_path, "STFA", {
        { "DATA", saturn_favorite_anim_data_handler }
    });
}

void saturn_save_favorite_anims() {
    char anim_favorites_path[SYS_MAX_PATH] = "";
    strncat(anim_favorites_path,  sys_user_path(), SYS_MAX_PATH);
    strncat(anim_favorites_path, "/dynos/anim_favorites.bin", SYS_MAX_PATH);

    SaturnFormatStream stream = saturn_format_output("STFA", curr_ver);
    saturn_format_new_section(&stream, "DATA");
    saturn_format_write_int32(&stream, favorite_anims.size());
    for (int anim : favorite_anims) {
        saturn_format_write_int32(&stream, anim);
    }
    saturn_format_close_section(&stream);
    saturn_format_write(anim_favorites_path, &stream);
}