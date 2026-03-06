/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and display the 2-page dashboard UI.
 *
 * Page 1 – Peripheral Monitor: shows peripheral status cards.
 *   Tapping a card opens a simple info overlay (title, status badge,
 *   description, back button).
 *
 * Page 2 – Debug Tools: shows debug-tool cards.
 *   Tapping a card opens an action overlay (title, description,
 *   "Run Tool" and "View Logs" buttons, back button).
 */
void lv_ui_dashboard_init(void);

#ifdef __cplusplus
}
#endif
