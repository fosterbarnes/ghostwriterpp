/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FONTSETTINGSDIALOG_H
#define FONTSETTINGSDIALOG_H

#include <QDialog>

namespace ghostwriter
{
class FontSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FontSettingsDialog(QWidget *parent = nullptr);
};

} // namespace ghostwriter

#endif
