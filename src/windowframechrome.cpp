/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "windowframechrome.h"

#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winerror.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif
#endif

namespace ghostwriterpp
{
void applyDarkModeToWindowFrame(QWidget *widget, bool dark)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }

    QWindow *window = widget->windowHandle();
    if (!window) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    const BOOL useDark = dark ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(hwnd,
                                       DWMWA_USE_IMMERSIVE_DARK_MODE,
                                       &useDark,
                                       sizeof(useDark));
    if (FAILED(hr)) {
        DwmSetWindowAttribute(hwnd,
                              DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
                              &useDark,
                              sizeof(useDark));
    }
#else
    Q_UNUSED(widget);
    Q_UNUSED(dark);
#endif
}
}
