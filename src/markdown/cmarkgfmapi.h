/*
 * SPDX-FileCopyrightText: 2020-2023 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CMARK_PROCESSOR_H
#define CMARK_PROCESSOR_H

#include <QScopedPointer>

#include "markdownast.h"
#include "previeweditmetadata.h"

namespace ghostwriterpp
{
/**
 * This class wraps the cmark-gfm API to make it thread-safe.
 */
class CmarkGfmAPIPrivate;
class CmarkGfmAPI
{
    Q_DECLARE_PRIVATE(CmarkGfmAPI)

public:
    /**
     * Returns the single instance of this class.
     */
    static CmarkGfmAPI *instance();

    /**
     * Destructor.
     */
    ~CmarkGfmAPI();

    /**
     * Parses the given Markdown text, returning an AST representation.
     * of the text.  Pass in true for smartTypographyEnabled to enable
     * smart typography.
     */
    MarkdownAST *parse(const QString &text, const bool smartTypographyEnabled);

    /**
     * Returns HTML text for the Markdown text.  Pass in true for
     * smartTypographyEnabled to enable smart typography.
     */
    QString renderToHtml(const QString &text, const bool smartTypographyEnabled);

    /**
     * Renders HTML for live preview with data-gw-* edit metadata (cmark-gfm only).
     */
    QString renderToHtmlWithPreviewEditMetadata(const QString &text, const bool smartTypographyEnabled);

    /**
     * Parses the current markdown and extracts a plain-text + source-range map for the
     * CMARK TEXT descendants of the element identified by [elementSourceStart,
     * elementSourceEndExclusive). Used by the preview-edit session to translate DOM-level
     * edits back to surgical source-range replacements, leaving inline structure intact.
     */
    PreviewEditTextMap extractPreviewEditTextMap(const QString &markdown,
                                                 int elementSourceStart,
                                                 int elementSourceEndExclusive);

    PreviewEditTextMap extractPreviewEditTextMap(const QString &markdown,
                                                 int elementSourceStart,
                                                 int elementSourceEndExclusive,
                                                 bool allowSoftbreaks,
                                                 bool allowParagraphGaps);

    PreviewEditTextMap extractPreviewEditTextMap(const QString &markdown,
                                                 int elementSourceStart,
                                                 int elementSourceEndExclusive,
                                                 bool allowSoftbreaks,
                                                 bool allowParagraphGaps,
                                                 bool requirePlainTextOnly);

protected:
    /**
     * Constructor.
     */
    CmarkGfmAPI();

private:
    QScopedPointer<CmarkGfmAPIPrivate> d_ptr;

};
}

#endif
