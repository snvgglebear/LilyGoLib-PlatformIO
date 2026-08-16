# Adding a custom bitmap font to a watch face

`simple_face.cpp` currently gets its oversized clock digits by taking the
largest bitmap font LVGL ships (`lv_font_montserrat_48` - LVGL 9.2.2 doesn't
go any bigger) and applying a 2x `transform_scale` to the label. That's a
one-line fix but it's resampling an already-rasterized 48px bitmap, so past
roughly 1.3-1.5x it starts looking soft/pixelated instead of crisp.

For a font that's genuinely sharp at a large size, generate a custom bitmap
font at the exact pixel size you want, the same way this repo already does
for `src/new_interface/src/font/font_alibaba_*.c` and `logo_font_80.c`.

## Steps

1. **Get the tool.** Either the web converter at lvgl.io's font converter
   page, or the CLI (`npm install -g lv_font_conv`) if you want it
   scriptable/repeatable.
2. **Pick a source font.** Any TTF/OTF with a license that allows embedding.
   For a plain digital clock, a monospaced-digit face reads best, but it
   doesn't have to match Montserrat - it's a fresh bitmap either way.
3. **Converter settings** (matching how this project already builds fonts):
   - **Size** - whatever pixel height you want the digits (e.g. 96px for a
     full 2x-over-48 replacement).
   - **Bpp** - 4 (LVGL's usual anti-aliasing depth; matches the built-in
     Montserrat fonts).
   - **Range** - restrict to just what the clock needs:
     `0x20,0x2D,0x30-0x39,0x3A` covers space, `-` (for the `--:--` startup
     text), digits `0`-`9`, and `:`. Narrowing the range keeps the generated
     `.c` file (and flash usage) small - the full built-in Montserrat fonts
     are large because they cover the whole ASCII range plus symbols.
   - **Output format** - C file (`lvgl`), not binary - this project links
     fonts in as compiled `.c` sources, not runtime-loaded binaries.
4. **Name and place it.** Save it as e.g.
   `src/custom_interface/watch_faces/font_clock_96.c`, following the
   `font_<name>_<size>.c` naming already used in `src/new_interface/src/font/`.
5. **Declare and use it** in `simple_face.cpp`:

   ```cpp
   LV_FONT_DECLARE(font_clock_96);
   ...
   lv_obj_set_style_text_font(label_time, &font_clock_96, 0);
   ```

   and drop the `lv_obj_set_style_transform_scale()` / margin workaround
   currently around `label_time`.
6. **No build flag needed.** Unlike the built-in `LV_FONT_MONTSERRAT_*` sizes
   (toggled via `-D LV_FONT_MONTSERRAT_48=1` in `platformio.ini`), a
   converter-generated font is just a normal compiled `.c` file in the source
   tree; PlatformIO picks it up automatically since it lives under `src_dir`.
