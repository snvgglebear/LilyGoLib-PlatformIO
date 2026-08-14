/**
 * @file      DrawSD_BMP.ino
 * @license   MIT
 * @brief     Decode a BMP file from the SD card and push it to the display.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/DrawSD_BMP.
 *
 * The original leaned on TFT_eSPI's pushImage(). These boards have no TFT_eSPI --
 * the panel is QSPI and normally driven by LVGL -- so this decodes the bitmap by
 * hand and hands finished scanlines to instance.pushColors(), which is the
 * lowest-level blit LilyGoLib exposes.
 *
 * Only uncompressed 24-bit BMPs are handled, which is what "Save As -> BMP" in
 * any image editor produces. Copy one to the card as /image.bmp.
 *
 * Requires an SD socket, so this is T-Watch-Ultra only -- the T-Watch-S3 has no
 * card slot.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef HAS_SD_CARD_SOCKET

#include <SD.h>

#define BMP_PATH "/image.bmp"

static lv_obj_t *label1;

/// Little-endian readers -- BMP headers are always little-endian on disk.
static uint16_t read16(File &f)
{
    uint16_t r;
    f.read((uint8_t *)&r, sizeof(r));
    return r;
}

static uint32_t read32(File &f)
{
    uint32_t r;
    f.read((uint8_t *)&r, sizeof(r));
    return r;
}

static void status(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    lv_label_set_text(label1, buf);
    Serial.println(buf);
    lv_task_handler();
}

static bool draw_bmp(const char *path)
{
    File f = SD.open(path);
    if (!f) {
        status("Cannot open %s", path);
        return false;
    }

    if (read16(f) != 0x4D42) {          // 'BM'
        status("%s is not a BMP", path);
        f.close();
        return false;
    }

    read32(f);                           // file size, unused
    read32(f);                           // reserved
    uint32_t pixel_offset = read32(f);   // where the pixel array starts
    read32(f);                           // DIB header size
    int32_t  width  = (int32_t)read32(f);
    int32_t  height = (int32_t)read32(f);
    read16(f);                           // colour planes
    uint16_t depth  = read16(f);
    uint32_t compression = read32(f);

    if (depth != 24 || compression != 0) {
        status("Need uncompressed 24-bit BMP\n(got %u-bit, compression %lu)",
               depth, (unsigned long)compression);
        f.close();
        return false;
    }

    // A positive height means the rows are stored bottom-up, which is the usual
    // case; negative means top-down.
    bool bottom_up = height > 0;
    if (height < 0) height = -height;

    Serial.printf("BMP %ldx%ld, %u-bit\n", (long)width, (long)height, depth);

    // Each row is padded out to a 4-byte boundary.
    uint32_t row_bytes = ((uint32_t)width * 3 + 3) & ~3u;

    uint8_t  *row = (uint8_t *)malloc(row_bytes);
    uint16_t *line = (uint16_t *)malloc((size_t)width * sizeof(uint16_t));
    if (!row || !line) {
        status("Out of memory for %ld px row", (long)width);
        free(row); free(line); f.close();
        return false;
    }

    int32_t screen_w = instance.width();
    int32_t screen_h = instance.height();
    int32_t draw_w = width  < screen_w ? width  : screen_w;
    int32_t draw_h = height < screen_h ? height : screen_h;

    for (int32_t y = 0; y < draw_h; y++) {
        // Pick the source row that lands on screen row y.
        int32_t src_y = bottom_up ? (height - 1 - y) : y;
        f.seek(pixel_offset + (uint32_t)src_y * row_bytes);
        f.read(row, row_bytes);

        for (int32_t x = 0; x < draw_w; x++) {
            // BMP stores BGR; convert to the RGB565 the panel wants.
            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            line[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // One scanline at a time keeps the RAM cost to a single row.
        instance.pushColors(0, y, draw_w - 1, y, line);
    }

    free(row);
    free(line);
    f.close();
    Serial.println("BMP drawn");
    return true;
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    if (!instance.installSD()) {
        status("No SD card detected");
        return;
    }

    status("Loading " BMP_PATH " ...");
    delay(500);

    // LVGL owns the framebuffer; stop it repainting over the bitmap we are about
    // to blit straight to the panel.
    lv_obj_add_flag(label1, LV_OBJ_FLAG_HIDDEN);
    lv_task_handler();

    if (!draw_bmp(BMP_PATH)) {
        lv_obj_remove_flag(label1, LV_OBJ_FLAG_HIDDEN);
    }
}

void loop()
{
    // Deliberately not calling lv_task_handler() after a successful draw: LVGL
    // would immediately redraw its own screen over the bitmap.
    delay(1000);
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
