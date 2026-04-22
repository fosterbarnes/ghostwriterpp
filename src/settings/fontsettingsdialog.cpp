/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "appsettings.h"
#include "fontsettingsdialog.h"
#include "simplefontdialog.h"

namespace ghostwriterpp
{
namespace
{
QString fontSummary(const QFont &font)
{
    return FontSettingsDialog::tr("%1 %2pt")
        .arg(font.family())
        .arg(font.pointSize());
}
} // namespace

FontSettingsDialog::FontSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Fonts"));

    AppSettings *appSettings = AppSettings::instance();

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    auto *editorLine = new QLineEdit(fontSummary(appSettings->editorFont()));
    editorLine->setReadOnly(true);
    auto *editorRow = new QHBoxLayout();
    editorRow->addWidget(editorLine);
    auto *editorBtn = new QPushButton(tr("Choose..."));
    editorRow->addWidget(editorBtn);
    form->addRow(tr("Editor:"), editorRow);
    connect(editorBtn, &QPushButton::clicked, this, [=]() {
        bool ok = false;
        QFont font = SimpleFontDialog::font(&ok, appSettings->editorFont(), this);
        if (ok) {
            editorLine->setText(fontSummary(font));
            appSettings->setEditorFont(font);
        }
    });

    auto *previewTextLine = new QLineEdit(fontSummary(appSettings->previewTextFont()));
    previewTextLine->setReadOnly(true);
    auto *previewTextRow = new QHBoxLayout();
    previewTextRow->addWidget(previewTextLine);
    auto *previewTextBtn = new QPushButton(tr("Choose..."));
    previewTextRow->addWidget(previewTextBtn);
    form->addRow(tr("Preview text:"), previewTextRow);
    connect(previewTextBtn, &QPushButton::clicked, this, [=]() {
        bool ok = false;
        QFont font = SimpleFontDialog::font(&ok, appSettings->previewTextFont(), this);
        if (ok) {
            previewTextLine->setText(fontSummary(font));
            appSettings->setPreviewTextFont(font);
        }
    });

    auto *previewCodeLine = new QLineEdit(fontSummary(appSettings->previewCodeFont()));
    previewCodeLine->setReadOnly(true);
    auto *previewCodeRow = new QHBoxLayout();
    previewCodeRow->addWidget(previewCodeLine);
    auto *previewCodeBtn = new QPushButton(tr("Choose..."));
    previewCodeRow->addWidget(previewCodeBtn);
    form->addRow(tr("Preview code:"), previewCodeRow);
    connect(previewCodeBtn, &QPushButton::clicked, this, [=]() {
        bool ok = false;
        QFont font = SimpleFontDialog::monospaceFont(&ok, appSettings->previewCodeFont(), this);
        if (ok) {
            previewCodeLine->setText(fontSummary(font));
            appSettings->setPreviewCodeFont(font);
        }
    });

    layout->addLayout(form);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
}

} // namespace ghostwriterpp
