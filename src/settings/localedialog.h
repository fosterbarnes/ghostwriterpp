/*
 * SPDX-FileCopyrightText: 2016-2023 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOCALEDIALOG_H
#define LOCALEDIALOG_H

#include <QDialog>
#include <QScopedPointer>

namespace ghostwriterpp
{
/**
 * Displays a dialog with supported languages for the applications
 * from which the user may choose.
 * 
 * Note: This dialog is set to delete on close automatically.
 */
class LocaleDialogPrivate;
class LocaleDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * Constructor.
     */
    explicit LocaleDialog(QWidget *parent = nullptr);

    /**
     * Destructor.
     */
    virtual ~LocaleDialog();

private:
    QScopedPointer<LocaleDialogPrivate> d;

};
} // namespace ghostwriterpp

#endif // LOCALEDIALOG_H
