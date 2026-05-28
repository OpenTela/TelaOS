#include "hal/lv_fs_sd.h"

#if defined(LVGL_MOCK_ENABLED)
namespace LvFsSd { void registerDrive() {} }   // host: no LVGL FS
#else

#include "hal/sdcard.h"
#include "hal/sd_path.h"
#include "utils/log_config.h"
#include <lvgl.h>
#include <cstdio>

namespace LvFsSd {

static const char* TAG = "LvFsSd";

// LVGL strips the "A:" prefix and passes the remainder as `path`.
static void* fs_open(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
    char full[256];
    if (!sdResolvePath(Sd::mountPoint(), path, full, sizeof(full))) return nullptr;
    const char* m = (mode == LV_FS_MODE_WR) ? "wb"
                  : (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) ? "rb+"
                  : "rb";
    return fopen(full, m);
}

static lv_fs_res_t fs_close(lv_fs_drv_t*, void* fp) {
    fclose((FILE*) fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t*, void* fp, void* buf,
                           uint32_t btr, uint32_t* br) {
    *br = (uint32_t) fread(buf, 1, btr, (FILE*) fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_write(lv_fs_drv_t*, void* fp, const void* buf,
                            uint32_t btw, uint32_t* bw) {
    *bw = (uint32_t) fwrite(buf, 1, btw, (FILE*) fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t*, void* fp, uint32_t pos,
                           lv_fs_whence_t whence) {
    int w = (whence == LV_FS_SEEK_SET) ? SEEK_SET
          : (whence == LV_FS_SEEK_CUR) ? SEEK_CUR : SEEK_END;
    return fseek((FILE*) fp, (long) pos, w) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t*, void* fp, uint32_t* pos) {
    long p = ftell((FILE*) fp);
    if (p < 0) return LV_FS_RES_UNKNOWN;
    *pos = (uint32_t) p;
    return LV_FS_RES_OK;
}

static lv_fs_drv_t s_drv;

void registerDrive() {
    lv_fs_drv_init(&s_drv);
    s_drv.letter   = 'A';
    s_drv.open_cb  = fs_open;
    s_drv.close_cb = fs_close;
    s_drv.read_cb  = fs_read;
    s_drv.write_cb = fs_write;
    s_drv.seek_cb  = fs_seek;
    s_drv.tell_cb  = fs_tell;
    lv_fs_drv_register(&s_drv);
    LOG_I(Log::SD, "LVGL drive 'A:' -> %s", Sd::mountPoint());
}

} // namespace LvFsSd

#endif // LVGL_MOCK_ENABLED
