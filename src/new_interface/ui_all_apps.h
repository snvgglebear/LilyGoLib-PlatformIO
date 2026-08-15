/**
 * @file      ui_all_apps.h
 * @license   MIT
 * @brief     Full app listing, reached from the home screen's pinned-links row.
 */
#pragma once

#include "ui_define.h"

/// Lists every app in ui_main.cpp's app_registry() as a row; tapping one
/// opens it via open_app(), same as the pinned-links row and the original
/// launcher icon row.
extern app_t ui_all_apps_main;
