#pragma once

/**
 * @file      images.h
 * @license   MIT
 * @brief     Every bitmap asset compiled into custom_interface, declared once.
 *
 * The .c files beside this header are the LVGL 9 variants of src/factory's
 * image set (src/factory/src/img_*_v9.c), copied rather than shared: the two
 * apps are separate src_dir trees and are never compiled together.
 *
 * Only the icons came across. Factory's two wallpapers (img_background,
 * img_background2) did not: their LVGL 9 files carry a 240x240 and a 480x222
 * bitmap for the S3 and the Pager, and the Ultra's 410x502 branch exists only
 * inside the multi-board LVGL 8 source. Nothing here wants one anyway -- the
 * watch faces draw on black.
 *
 * All of these are RGB565A8, i.e. 3 bytes per pixel including the alpha plane,
 * so an 80x80 icon is ~19 KB of flash. They are linked in whether or not
 * anything draws them, so delete the .c rather than only the declaration if a
 * consumer goes away.
 *
 * Sizes are as generated -- the artwork was cut for factory's launcher grid,
 * not for anything here, so expect to lv_image_set_scale() rather than to find
 * a size that happens to fit.
 */

#include <lvgl.h>

/*Peripherals / radios ---------------------------------------------------*/
LV_IMAGE_DECLARE(img_wifi);          ///< 80x80
LV_IMAGE_DECLARE(img_bluetooth);     ///< 80x80
LV_IMAGE_DECLARE(img_gps);           ///< 70x70
LV_IMAGE_DECLARE(img_radio);         ///< 65x65
LV_IMAGE_DECLARE(img_walkie);        ///< 65x65

/*Apps / activities ------------------------------------------------------*/
LV_IMAGE_DECLARE(img_msgchat);       ///< 80x80
LV_IMAGE_DECLARE(img_music);         ///< 80x80
LV_IMAGE_DECLARE(img_keyboard);      ///< 80x80
LV_IMAGE_DECLARE(img_microphone);    ///< 65x65
LV_IMAGE_DECLARE(img_monitoring);    ///< 70x70
LV_IMAGE_DECLARE(img_configuration); ///< 70x70
LV_IMAGE_DECLARE(img_gyroscope);     ///< 60x60
LV_IMAGE_DECLARE(img_test);          ///< 80x80

/*Status / decoration ----------------------------------------------------*/
LV_IMAGE_DECLARE(img_battery);       ///< 31x24 -- the outline simple_face fills
LV_IMAGE_DECLARE(img_power);         ///< 65x65
LV_IMAGE_DECLARE(img_cry);           ///< 96x80
LV_IMAGE_DECLARE(img_dog);           ///< 32x32
