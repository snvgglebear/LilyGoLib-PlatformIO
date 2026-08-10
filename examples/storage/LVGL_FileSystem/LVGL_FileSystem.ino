/**
 * @file      LVGL_FileSystem.ino
 * @license   MIT
 * @brief     Register the SD card with LVGL so widgets can load files by path.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/lvgl_fs.
 *
 * Out of the box LVGL can only show images that were compiled into the firmware
 * as C arrays. Registering a filesystem driver changes that: after this runs,
 * any widget can be pointed at "S:/photo.png" and LVGL will stream it off the
 * card. That is what makes user-supplied wallpapers or downloaded assets
 * possible without a reflash.
 *
 * The driver is a small shim -- LVGL asks for open/close/read/seek/tell, and
 * each one forwards to the Arduino SD library.
 *
 * Put a PNG at /photo.png on the card. Requires an SD socket, so T-Watch-Ultra
 * only.
 *
 * @see https://docs.lvgl.io/master/details/main-components/fs.html
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef HAS_SD_CARD_SOCKET

#include <SD.h>

/// Drive letter LVGL paths are prefixed with, i.e. "S:/photo.png".
#define LV_SD_LETTER 'S'
#define IMAGE_PATH   "S:/photo.png"

static lv_fs_drv_t sd_drv;
static lv_obj_t *label1;

/* ---------------------------------------------------------------------------
 * LVGL filesystem callbacks. LVGL hands us an opaque void* per open file; we
 * store a heap-allocated Arduino File there.
 * ------------------------------------------------------------------------- */

static void *sd_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    // LVGL strips the "S:" prefix before calling us, so `path` is "/photo.png".
    const char *flags = (mode == LV_FS_MODE_WR) ? FILE_WRITE : FILE_READ;

    File f = SD.open(path, flags);
    if (!f) {
        return NULL;
    }

    File *handle = new File(f);
    return handle;
}

static lv_fs_res_t sd_close(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);
    File *f = (File *)file_p;
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);
    File *f = (File *)file_p;
    *br = f->read((uint8_t *)buf, btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    File *f = (File *)file_p;

    uint32_t base = 0;
    switch (whence) {
    case LV_FS_SEEK_SET: base = 0;            break;
    case LV_FS_SEEK_CUR: base = f->position(); break;
    case LV_FS_SEEK_END: base = f->size();     break;
    }

    return f->seek(base + pos) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t sd_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    LV_UNUSED(drv);
    File *f = (File *)file_p;
    *pos_p = f->position();
    return LV_FS_RES_OK;
}

static void register_sd_with_lvgl()
{
    lv_fs_drv_init(&sd_drv);

    sd_drv.letter   = LV_SD_LETTER;
    sd_drv.open_cb  = sd_open;
    sd_drv.close_cb = sd_close;
    sd_drv.read_cb  = sd_read;
    sd_drv.seek_cb  = sd_seek;
    sd_drv.tell_cb  = sd_tell;

    lv_fs_drv_register(&sd_drv);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_align(label1, LV_ALIGN_BOTTOM_MID, 0, -10);

    if (!instance.installSD()) {
        lv_label_set_text(label1, "No SD card detected");
        Serial.println("No SD card");
        return;
    }

    register_sd_with_lvgl();
    Serial.println("SD registered with LVGL as drive '" "S" "'");

    // From here on the path is all LVGL needs -- no manual file handling.
    lv_obj_t *img = lv_image_create(lv_scr_act());
    lv_image_set_src(img, IMAGE_PATH);
    lv_obj_center(img);

    lv_label_set_text(label1, "Loaded " IMAGE_PATH);

    // If the file is missing or not a format LVGL was built with, the image
    // widget simply stays empty -- check the serial log and the card contents.
    Serial.println("If the screen is blank, check " IMAGE_PATH " exists and PNG support is enabled");
}

void loop()
{
    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support boards with an SD card socket (T-Watch-Ultra)"); delay(1000);
}

#endif
