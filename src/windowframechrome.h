/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GHOSTWRITER_WINDOWFRAMECHROME_H
#define GHOSTWRITER_WINDOWFRAMECHROME_H

class QWidget;

namespace ghostwriter
{
/**
 * Syncs the native window frame (caption/title bar on Windows) with app dark mode.
 * Safe no-op on non-Windows. Call after the widget has a QWindow (e.g. after show).
 */
void applyDarkModeToWindowFrame(QWidget *widget, bool dark);
}

#endif
