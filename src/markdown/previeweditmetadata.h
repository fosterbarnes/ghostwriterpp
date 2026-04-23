/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PREVIEW_EDIT_METADATA_H
#define PREVIEW_EDIT_METADATA_H

#include <QString>
#include <vector>

struct cmark_node;

namespace ghostwriterpp
{

struct TextNodeSlot {
    int plainStart = 0;
    int plainEnd = 0;
    int sourceStart = 0;
    int sourceEnd = 0;
};

struct PreviewEditTextMap {
    QString plain;
    std::vector<TextNodeSlot> nodes;
    bool hasUntrackedText = false;
    bool valid = false;
    bool codeblockWholeSourceReplace = false;
};

QString augmentPreviewHtmlWithEditMetadata(const QString &markdown, cmark_node *root, QString html);

PreviewEditTextMap buildPreviewEditTextMap(const QString &markdown,
                                           cmark_node *root,
                                           int elementSourceStart,
                                           int elementSourceEndExclusive,
                                           bool allowSoftbreaks = false,
                                           bool allowParagraphGaps = false,
                                           bool requirePlainTextOnly = false);

int longestBacktickRun(const QString &s);

QString serializeInlineCodeRawInner(const QString &normalizedPlain, int openFenceLen);

bool previewCodeblockTextHasFenceLikeLine(const QString &text);

QString serializeIndentedCodeBlock(const QString &literalPlain);

bool previewListItemTextHasListStarter(const QString &text);

} // namespace ghostwriterpp

#endif
