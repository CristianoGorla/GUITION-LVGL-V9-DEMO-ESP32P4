/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * lv_ui.c  –  2-page swipeable dashboard
 *
 * Page 0 – Peripheral Monitor
 *   Peripheral cards → show_peripheral_overlay()
 *   Overlay: title, status badge, description, Back button (no action buttons)
 *
 * Page 1 – Debug Tools
 *   Debug-tool cards → show_debug_tool_overlay()
 *   Overlay: title, description, "Run Tool" (green) + "View Logs" (blue) + Back
 */

#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "lv_ui.h"

static const char *TAG = "lv_ui";

/* ─── Layout constants ─────────────────────────────────────────────────────── */
#define NUM_PAGES           2
#define CARD_W              200
#define CARD_H              120
#define CARD_PAD            16
#define HEADER_H            56
#define INDICATOR_SIZE      10
#define INDICATOR_GAP       14

/* ─── Peripheral data ──────────────────────────────────────────────────────── */
typedef enum {
    PERIPH_STATUS_OK   = 0,
    PERIPH_STATUS_WARN = 1,
    PERIPH_STATUS_ERR  = 2,
} periph_status_t;

typedef struct {
    const char     *name;
    periph_status_t status;
    const char     *description;
} periph_info_t;

static const periph_info_t s_peripherals[] = {
    { "Display LCD",    PERIPH_STATUS_OK,   "LCD JD9165 480×800\nBacklight: ON\nTouch: GT911 active" },
    { "Audio Codec",    PERIPH_STATUS_OK,   "ES8311 codec\nPlayback: ready\nRecord: ready" },
    { "PSRAM",          PERIPH_STATUS_OK,   "8 MB PSRAM\nSpeed: 80 MHz\nUsage: normal" },
    { "Flash Storage",  PERIPH_STATUS_WARN, "16 MB NOR Flash\nUsage: 72%\nCheck free space" },
    { "Wi-Fi",          PERIPH_STATUS_ERR,  "Not configured\nSSID: none\nConnect to network" },
    { "Camera",         PERIPH_STATUS_WARN, "OV5647 sensor\nStream: idle\nCalibrate required" },
};
#define NUM_PERIPHERALS  (sizeof(s_peripherals) / sizeof(s_peripherals[0]))

/* ─── Debug-tool data ──────────────────────────────────────────────────────── */
typedef struct {
    const char *name;
    const char *description;
} debug_tool_info_t;

static const debug_tool_info_t s_debug_tools[] = {
    { "Log Monitor",    "Real-time ESP-IDF log viewer.\nFilters by tag and severity.\nStream to serial or display." },
    { "Heap Analyzer",  "Track heap usage over time.\nDetect memory leaks.\nShow fragmentation stats." },
    { "GPIO Inspector", "Read/write GPIO states.\nMonitor interrupts.\nPulse-width measurement." },
    { "I2C Scanner",    "Scan all I2C buses.\nList device addresses.\nRead/write registers." },
    { "SPI Monitor",    "Capture SPI transactions.\nDecode protocol frames.\nMeasure bus speed." },
    { "Task Profiler",  "List FreeRTOS tasks.\nShow CPU usage per task.\nStack high-water marks." },
};
#define NUM_DEBUG_TOOLS  (sizeof(s_debug_tools) / sizeof(s_debug_tools[0]))

/* ─── Global UI objects ────────────────────────────────────────────────────── */
static lv_obj_t *g_tileview      = NULL;
static lv_obj_t *g_indicators[NUM_PAGES];
static lv_obj_t *g_overlay       = NULL;   /* currently open overlay (if any) */

/* ══════════════════════════════════════════════════════════════════════════════
 *  FORWARD DECLARATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */
static void show_peripheral_overlay(const periph_info_t *info);
static void show_debug_tool_overlay(const char *tool_name, const char *description);
static void close_overlay(lv_obj_t *overlay);

/* ══════════════════════════════════════════════════════════════════════════════
 *  DEBUG-TOOL CALLBACKS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_debug_tool_run_clicked(lv_event_t *e)
{
    const char *tool_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Run tool: %s", tool_name);
    /* TODO: chiamata a main.c per eseguire tool */
}

static void on_debug_tool_logs_clicked(lv_event_t *e)
{
    const char *tool_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "View logs: %s", tool_name);
    /* TODO: mostra schermata log specifica */
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  OVERLAY HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_back_btn_clicked(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    close_overlay(overlay);
}

static void close_overlay(lv_obj_t *overlay)
{
    if (overlay && lv_obj_is_valid(overlay)) {
        lv_obj_del(overlay);
    }
    g_overlay = NULL;
}

/**
 * @brief Create the common overlay container (full-screen dimmed panel + card).
 *
 * @return Pointer to the inner card (lv_obj_t *) where content should be added.
 *         The outer overlay (dimmer) is stored in g_overlay.
 */
static lv_obj_t *create_overlay_card(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_coord_t sw    = lv_obj_get_width(screen);
    lv_coord_t sh    = lv_obj_get_height(screen);

    /* Semi-transparent background – blocks input to the tiles behind */
    lv_obj_t *dimmer = lv_obj_create(screen);
    lv_obj_set_size(dimmer, sw, sh);
    lv_obj_align(dimmer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dimmer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dimmer, LV_OPA_50, 0);
    lv_obj_set_style_border_width(dimmer, 0, 0);
    lv_obj_set_style_radius(dimmer, 0, 0);
    lv_obj_clear_flag(dimmer, LV_OBJ_FLAG_SCROLLABLE);

    g_overlay = dimmer;

    /* White card centred on the dimmer */
    lv_obj_t *card = lv_obj_create(dimmer);
    lv_obj_set_size(card, sw - 60, sh - 120);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    return card;
}

/* ── Status badge helpers ───────────────────────────────────────────────────── */

static lv_color_t status_color(periph_status_t s)
{
    switch (s) {
    case PERIPH_STATUS_OK:   return lv_color_hex(0x4CAF50);
    case PERIPH_STATUS_WARN: return lv_color_hex(0xFF9800);
    default:                 return lv_color_hex(0xF44336);
    }
}

static const char *status_text(periph_status_t s)
{
    switch (s) {
    case PERIPH_STATUS_OK:   return "OK";
    case PERIPH_STATUS_WARN: return "WARN";
    default:                 return "ERR";
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PERIPHERAL OVERLAY  (simple: title + badge + description + back)
 * ══════════════════════════════════════════════════════════════════════════════ */

static void show_peripheral_overlay(const periph_info_t *info)
{
    if (g_overlay) {
        close_overlay(g_overlay);
    }

    lv_obj_t *card = create_overlay_card();

    /* Title */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, info->name);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Status badge */
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 70, 28);
    lv_obj_align_to(badge, lbl_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
    lv_obj_set_style_bg_color(badge, status_color(info->status), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 14, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_status = lv_label_create(badge);
    lv_label_set_text(lbl_status, status_text(info->status));
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);

    /* Description */
    lv_obj_t *lbl_desc = lv_label_create(card);
    lv_label_set_text(lbl_desc, info->description);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_desc, lv_obj_get_width(card) - 40);
    lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(lbl_desc, badge, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(card);
    lv_obj_set_size(btn_back, 120, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_radius(btn_back, 20, 0);
    lv_obj_add_event_cb(btn_back, on_back_btn_clicked, LV_EVENT_CLICKED, g_overlay);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(lbl_back, lv_color_white(), 0);
    lv_obj_center(lbl_back);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  DEBUG-TOOL OVERLAY  (title + description + Run Tool + View Logs + back)
 * ══════════════════════════════════════════════════════════════════════════════ */

static void show_debug_tool_overlay(const char *tool_name, const char *description)
{
    if (g_overlay) {
        close_overlay(g_overlay);
    }

    lv_obj_t *card = create_overlay_card();

    /* Title */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, tool_name);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Description */
    lv_obj_t *lbl_desc = lv_label_create(card);
    lv_label_set_text(lbl_desc, description);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_desc, lv_obj_get_width(card) - 40);
    lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(lbl_desc, lbl_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    /* ── Action buttons row ─────────────────────────────────────────────────── */

    /* "Run Tool" button (green) */
    lv_obj_t *btn_run = lv_btn_create(card);
    lv_obj_set_size(btn_run, 130, 44);
    lv_obj_align(btn_run, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_run, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(btn_run, 22, 0);
    lv_obj_add_event_cb(btn_run, on_debug_tool_run_clicked, LV_EVENT_CLICKED, (void *)tool_name);

    lv_obj_t *lbl_run = lv_label_create(btn_run);
    lv_label_set_text(lbl_run, LV_SYMBOL_PLAY " Run Tool");
    lv_obj_set_style_text_color(lbl_run, lv_color_white(), 0);
    lv_obj_center(lbl_run);

    /* "View Logs" button (blue) */
    lv_obj_t *btn_logs = lv_btn_create(card);
    lv_obj_set_size(btn_logs, 130, 44);
    lv_obj_align(btn_logs, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_logs, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(btn_logs, 22, 0);
    lv_obj_add_event_cb(btn_logs, on_debug_tool_logs_clicked, LV_EVENT_CLICKED, (void *)tool_name);

    lv_obj_t *lbl_logs = lv_label_create(btn_logs);
    lv_label_set_text(lbl_logs, LV_SYMBOL_LIST " View Logs");
    lv_obj_set_style_text_color(lbl_logs, lv_color_white(), 0);
    lv_obj_center(lbl_logs);

    /* "Back" button */
    lv_obj_t *btn_back = lv_btn_create(card);
    lv_obj_set_size(btn_back, 100, 44);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_radius(btn_back, 22, 0);
    lv_obj_add_event_cb(btn_back, on_back_btn_clicked, LV_EVENT_CLICKED, g_overlay);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(lbl_back, lv_color_white(), 0);
    lv_obj_center(lbl_back);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  CARD CLICK CALLBACKS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_peripheral_card_clicked(lv_event_t *e)
{
    const periph_info_t *info = (const periph_info_t *)lv_event_get_user_data(e);
    show_peripheral_overlay(info);
}

static void on_debug_card_clicked(lv_event_t *e)
{
    const debug_tool_info_t *tool = (const debug_tool_info_t *)lv_event_get_user_data(e);
    show_debug_tool_overlay(tool->name, tool->description);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PAGE INDICATOR UPDATE
 * ══════════════════════════════════════════════════════════════════════════════ */

static void update_page_indicators(int active_page)
{
    for (int i = 0; i < NUM_PAGES; i++) {
        if (g_indicators[i]) {
            lv_obj_set_style_bg_color(
                g_indicators[i],
                (i == active_page) ? lv_color_white() : lv_color_hex(0x555555),
                0);
        }
    }
}

static void on_tileview_value_changed(lv_event_t *e)
{
    lv_obj_t *tv     = lv_event_get_target(e);
    lv_obj_t *active = lv_tileview_get_tile_active(tv);

    /* Find which column (page) is active */
    for (int i = 0; i < NUM_PAGES; i++) {
        lv_obj_t *tile = lv_obj_get_child(tv, i);
        if (tile == active) {
            update_page_indicators(i);
            break;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  CARD BUILDER HELPERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static lv_color_t periph_card_color(periph_status_t s)
{
    switch (s) {
    case PERIPH_STATUS_OK:   return lv_color_hex(0x1E3A2F);
    case PERIPH_STATUS_WARN: return lv_color_hex(0x3A2E1E);
    default:                 return lv_color_hex(0x3A1E1E);
    }
}

static void create_peripheral_card(lv_obj_t *parent, const periph_info_t *info)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, periph_card_color(info->status), 0);
    lv_obj_set_style_border_color(card, status_color(info->status), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    /* Peripheral name */
    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, info->name);
    lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Status badge (small pill) */
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 52, 22);
    lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(badge, status_color(info->status), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 11, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_st = lv_label_create(badge);
    lv_label_set_text(lbl_st, status_text(info->status));
    lv_obj_set_style_text_color(lbl_st, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_st, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_st, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(card, on_peripheral_card_clicked, LV_EVENT_CLICKED, (void *)info);
}

static void create_debug_tool_card(lv_obj_t *parent, const debug_tool_info_t *tool)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2A3A), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, tool->name);
    lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Small "RUN" badge */
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 52, 22);
    lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 11, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_run = lv_label_create(badge);
    lv_label_set_text(lbl_run, "TOOL");
    lv_obj_set_style_text_color(lbl_run, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_run, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_run, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(card, on_debug_card_clicked, LV_EVENT_CLICKED, (void *)tool);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PAGE BUILDERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void build_page_peripherals(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    /* Page header */
    lv_obj_t *header = lv_obj_create(tile);
    lv_obj_set_size(header, lv_pct(100), HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Peripheral Monitor");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable card grid */
    lv_obj_t *grid = lv_obj_create(tile);
    lv_obj_set_size(grid, lv_pct(100), lv_obj_get_height(tile) - HEADER_H);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, CARD_PAD, 0);
    lv_obj_set_style_pad_gap(grid, CARD_PAD, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (size_t i = 0; i < NUM_PERIPHERALS; i++) {
        create_peripheral_card(grid, &s_peripherals[i]);
    }
}

static void build_page_debug_tools(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    /* Page header */
    lv_obj_t *header = lv_obj_create(tile);
    lv_obj_set_size(header, lv_pct(100), HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Debug Tools");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable card grid */
    lv_obj_t *grid = lv_obj_create(tile);
    lv_obj_set_size(grid, lv_pct(100), lv_obj_get_height(tile) - HEADER_H);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, CARD_PAD, 0);
    lv_obj_set_style_pad_gap(grid, CARD_PAD, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (size_t i = 0; i < NUM_DEBUG_TOOLS; i++) {
        create_debug_tool_card(grid, &s_debug_tools[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PAGE INDICATOR BAR  (2 dots)
 * ══════════════════════════════════════════════════════════════════════════════ */

static void build_page_indicators(lv_obj_t *parent)
{
    lv_coord_t sw    = lv_obj_get_width(parent);
    lv_coord_t sh    = lv_obj_get_height(parent);
    lv_coord_t total = NUM_PAGES * INDICATOR_SIZE + (NUM_PAGES - 1) * INDICATOR_GAP;
    lv_coord_t x0    = (sw - total) / 2;
    lv_coord_t y     = sh - 20;

    for (int i = 0; i < NUM_PAGES; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, INDICATOR_SIZE, INDICATOR_SIZE);
        lv_obj_set_pos(dot, x0 + i * (INDICATOR_SIZE + INDICATOR_GAP), y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot,
            (i == 0) ? lv_color_white() : lv_color_hex(0x555555), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        g_indicators[i] = dot;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════════════════ */

void lv_ui_dashboard_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1C1E), 0);

    /* ── Tileview: 2 horizontal tiles ──────────────────────────────────────── */
    g_tileview = lv_tileview_create(screen);
    lv_obj_set_size(g_tileview, lv_pct(100), lv_pct(100));
    lv_obj_align(g_tileview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(g_tileview, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_border_width(g_tileview, 0, 0);
    lv_obj_set_style_pad_all(g_tileview, 0, 0);
    /* Remove the scroll-bar that tileview adds by default */
    lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tile0 = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *tile1 = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_LEFT);

    build_page_peripherals(tile0);
    build_page_debug_tools(tile1);

    /* ── Page-change callback ───────────────────────────────────────────────── */
    lv_obj_add_event_cb(g_tileview, on_tileview_value_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Page indicators (2 dots) ──────────────────────────────────────────── */
    build_page_indicators(screen);

    ESP_LOGI(TAG, "Dashboard UI initialised (2 pages)");
}
