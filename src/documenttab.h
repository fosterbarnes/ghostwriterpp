/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DOCUMENTTAB_H
#define DOCUMENTTAB_H

#include <QObject>
#include <QPointer>

#include "editor/colorscheme.h"

namespace ghostwriterpp
{
class MarkdownEditor;
class MarkdownDocument;
class DocumentManager;
class HtmlPreview;
class SpellCheckDecorator;
class DocumentStatistics;

/**
 * Owns the per-tab state: editor, document, document manager, preview,
 * spell checker, and document statistics. Wires their cross connections
 * so the window only has to worry about window-scoped signals on switch.
 */
class DocumentTab : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTab(const ColorScheme &colors, QObject *parent = nullptr);
    ~DocumentTab() override;

    MarkdownEditor *editor() const;
    MarkdownDocument *document() const;
    DocumentManager *documentManager() const;
    HtmlPreview *htmlPreview() const;
    SpellCheckDecorator *spelling() const;
    DocumentStatistics *documentStats() const;

    void applyColorScheme(const ColorScheme &colors);

    /**
     * Removes and destroys the HTML preview (e.g. before application quit
     * so WebEngine tears down with a running event loop).
     */
    void releaseHtmlPreview();

private:
    QPointer<MarkdownEditor> m_editor;
    QPointer<MarkdownDocument> m_document;
    QPointer<DocumentManager> m_documentManager;
    QPointer<HtmlPreview> m_htmlPreview;
    QPointer<SpellCheckDecorator> m_spelling;
    QPointer<DocumentStatistics> m_documentStats;
};
} // namespace ghostwriterpp

#endif // DOCUMENTTAB_H
