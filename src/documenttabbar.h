/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GHOSTWRITER_DOCUMENTTABBAR_H
#define GHOSTWRITER_DOCUMENTTABBAR_H

#include <QTabBar>

namespace ghostwriter
{
class DocumentTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

protected:
    void tabLayoutChange() override;
};
}

#endif
