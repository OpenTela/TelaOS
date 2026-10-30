// sim_tela_fs.cpp — LVGL drive "C:" of the simulated watch.
//
// TelaOS's lv_conf.h maps "C:" to stdio under /littlefs: on the watch that is the LittleFS
// partition the firmware itself writes to. Here the same lv_fs_stdio_init() registers a
// driver over the simulated LittleFS, so LVGL sees exactly what was flashed (data/ plus any
// --extra-data fixtures) and whatever the firmware writes later. LVGL's own lv_fs_stdio.c
// is left out of the build in exchange (CMakeLists.txt).
#include "LittleFS.h"  // TelaCI's in-memory LittleFS
#include "lvgl.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct FlashFile {
    std::string path;
    std::vector<uint8_t> data;  // a copy: the firmware may rewrite the file while it is open
    uint32_t pos = 0;
    bool write = false;
};

void* fsOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
    auto* f = new FlashFile;
    f->path = fs_normalize(path);
    f->write = (mode & LV_FS_MODE_WR) != 0;
    auto it = LittleFS.files.find(f->path);
    if (it != LittleFS.files.end()) {
        if (!f->write || (mode & LV_FS_MODE_RD)) f->data = it->second;  // "wb" truncates, "rb+" keeps
    } else if (!f->write) {
        delete f;
        return nullptr;
    }
    return f;
}

lv_fs_res_t fsClose(lv_fs_drv_t*, void* file) {
    auto* f = static_cast<FlashFile*>(file);
    if (f->write) LittleFS.files[f->path] = f->data;
    delete f;
    return LV_FS_RES_OK;
}

lv_fs_res_t fsRead(lv_fs_drv_t*, void* file, void* buf, uint32_t btr, uint32_t* br) {
    auto* f = static_cast<FlashFile*>(file);
    const uint32_t n = f->pos < f->data.size() ? std::min<uint32_t>(btr, f->data.size() - f->pos) : 0;
    std::memcpy(buf, f->data.data() + f->pos, n);
    f->pos += n;
    *br = n;
    return LV_FS_RES_OK;
}

lv_fs_res_t fsWrite(lv_fs_drv_t*, void* file, const void* buf, uint32_t btw, uint32_t* bw) {
    auto* f = static_cast<FlashFile*>(file);
    if (f->data.size() < f->pos + btw) f->data.resize(f->pos + btw);
    std::memcpy(f->data.data() + f->pos, buf, btw);
    f->pos += btw;
    *bw = btw;
    return LV_FS_RES_OK;
}

lv_fs_res_t fsSeek(lv_fs_drv_t*, void* file, uint32_t pos, lv_fs_whence_t whence) {
    auto* f = static_cast<FlashFile*>(file);
    const int64_t base = whence == LV_FS_SEEK_CUR ? f->pos : whence == LV_FS_SEEK_END ? f->data.size() : 0;
    f->pos = static_cast<uint32_t>(std::max<int64_t>(0, base + pos));
    return LV_FS_RES_OK;
}

lv_fs_res_t fsTell(lv_fs_drv_t*, void* file, uint32_t* pos) {
    *pos = static_cast<FlashFile*>(file)->pos;
    return LV_FS_RES_OK;
}

}  // namespace

extern "C" void lv_fs_stdio_init(void) {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = LV_FS_STDIO_LETTER;
    drv.cache_size = LV_FS_STDIO_CACHE_SIZE;
    drv.open_cb = fsOpen;
    drv.close_cb = fsClose;
    drv.read_cb = fsRead;
    drv.write_cb = fsWrite;
    drv.seek_cb = fsSeek;
    drv.tell_cb = fsTell;
    lv_fs_drv_register(&drv);
}
