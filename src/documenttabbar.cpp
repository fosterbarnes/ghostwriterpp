/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "documenttabbar.h"

#include <QtGlobal>

namespace ghostwriter
{

DocumentTabBar::DocumentTabBar(QWidget *parent)
    : QTabBar(parent)
{
}

void DocumentTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    if (count() <= 0) {
        setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        setMaximumWidth(sizeHint().width());
    }
}

}
