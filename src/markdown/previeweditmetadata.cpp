/*
 * SPDX-FileCopyrightText: 2026 Nate Peterson
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "previeweditmetadata.h"

#include <3rdparty/cmark-gfm/src/cmark-gfm.h>

#include <algorithm>
#include <QRegularExpression>
#include <QStringView>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace ghostwriterpp
{
namespace
{
bool lineUtf16Bounds(const QString &md, int line1, int &lineStart, int &lineEnd)
{
    int line = 1;
    int i = 0;
    const int n = md.size();

    while (line < line1 && i < n) {
        if (md[i] == QChar(u'\n')) {
            ++line;
        }
        ++i;
    }

    if (line != line1) {
        return false;
    }

    lineStart = i;
    while (i < n && md[i] != QChar(u'\n')) {
        ++i;
    }
    lineEnd = i;
    return true;
}

int columnOneBasedToIndexInLine(QStringView line, int col1)
{
    if (col1 <= 1) {
        return 0;
    }
    int col0 = col1 - 1;
    int idx = 0;
    const int n = line.size();

    while (col0 > 0 && idx < n) {
        const QChar ch = line[idx];
        if (ch.isHighSurrogate() && idx + 1 < n && line[idx + 1].isLowSurrogate()) {
            idx += 2;
        } else {
            idx += 1;
        }
        col0 -= 1;
    }
    return idx;
}

int lineNumberForUtf16Offset(const QString &md, int absOffset)
{
    if (absOffset <= 0) {
        return 1;
    }
    const int n = std::min(absOffset, static_cast<int>(md.size()));
    int line = 1;
    for (int i = 0; i < n; ++i) {
        if (md[i] == QChar(u'\n')) {
            ++line;
        }
    }
    return line;
}

bool nodeLineRangeViaAncestors(cmark_node *node, int &outStart, int &outEnd)
{
    for (cmark_node *a = node; a != nullptr; a = cmark_node_parent(a)) {
        const int sl = cmark_node_get_start_line(a);
        const int el = cmark_node_get_end_line(a);
        if (sl > 0 && el > 0) {
            outStart = sl;
            outEnd = el;
            return true;
        }
    }
    return false;
}

bool absoluteIndexForSourcePos(const QString &md, int line1, int col1, int &absOut)
{
    int ls = 0;
    int le = 0;
    if (!lineUtf16Bounds(md, line1, ls, le)) {
        return false;
    }
    QStringView lv = QStringView(md).mid(ls, le - ls);
    int off = columnOneBasedToIndexInLine(lv, col1);
    if (off > lv.size()) {
        return false;
    }
    absOut = ls + off;
    return true;
}

bool nodeUtf16RangeHalfOpen(const QString &md, cmark_node *node, int &absStart, int &absEndExclusive)
{
    const int sl = cmark_node_get_start_line(node);
    const int el = cmark_node_get_end_line(node);
    if (sl != el) {
        return false;
    }
    const int sc = cmark_node_get_start_column(node);
    const int ec = cmark_node_get_end_column(node);
    if (!absoluteIndexForSourcePos(md, sl, sc, absStart)) {
        return false;
    }
    int endCharIdx = 0;
    if (!absoluteIndexForSourcePos(md, el, ec, endCharIdx)) {
        return false;
    }
    absEndExclusive = endCharIdx + 1;
    return absStart >= 0 && absEndExclusive <= md.size() && absStart < absEndExclusive;
}

bool nodeUtf16RangeHalfOpenMultiline(const QString &md, cmark_node *node, int &absStart, int &absEndExclusive)
{
    const int sl = cmark_node_get_start_line(node);
    const int el = cmark_node_get_end_line(node);
    const int sc = cmark_node_get_start_column(node);
    const int ec = cmark_node_get_end_column(node);
    if (sl <= 0 || el <= 0 || sl > el) {
        return false;
    }
    if (!absoluteIndexForSourcePos(md, sl, sc, absStart)) {
        return false;
    }
    int endCharIdx = 0;
    if (!absoluteIndexForSourcePos(md, el, ec, endCharIdx)) {
        return false;
    }
    absEndExclusive = endCharIdx + 1;
    return absStart >= 0 && absEndExclusive <= md.size() && absStart < absEndExclusive;
}

bool cmarkInlineSourceRangeUtf16(const QString &md, cmark_node *node, int &absStart, int &absEndExclusive)
{
    if (nodeUtf16RangeHalfOpen(md, node, absStart, absEndExclusive)) {
        return true;
    }
    return nodeUtf16RangeHalfOpenMultiline(md, node, absStart, absEndExclusive);
}

void pushPlainNewlineSlot(std::vector<TextNodeSlot> &nodes,
                          QString &plain,
                          int &plainPos,
                          int &lastTextEndAbs,
                          int sourceStart,
                          int sourceEndExclusive)
{
    TextNodeSlot slot;
    slot.plainStart = plainPos;
    slot.plainEnd = plainPos + 1;
    slot.sourceStart = sourceStart;
    slot.sourceEnd = sourceEndExclusive;
    nodes.push_back(slot);
    plain += QChar(u'\n');
    plainPos += 1;
    lastTextEndAbs = sourceEndExclusive;
}

bool utf16RangeInsideElement(int s, int e, int elStart, int elEndExclusive)
{
    return s >= elStart && e <= elEndExclusive;
}

void setUntrackedIfInlineOverlaps(
    const QString &markdown, cmark_node *node, int elStart, int elEndExclusive, bool &hasUntracked)
{
    int a = 0;
    int b = 0;
    if (cmarkInlineSourceRangeUtf16(markdown, node, a, b) && b > elStart && a < elEndExclusive) {
        hasUntracked = true;
    }
}

bool htmlInlineLiteralIsBrTagOnly(const QString &lit)
{
    const QString t = lit.trimmed();
    return t.compare(QLatin1String("<br>"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("<br/>"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("<br />"), Qt::CaseInsensitive) == 0;
}

bool nodeIsTaskListItem(cmark_node *item)
{
    if (item == nullptr) {
        return false;
    }
    const char *ts = cmark_node_get_type_string(item);
    return ts != nullptr && std::strcmp(ts, "tasklist") == 0;
}

bool paragraphNodeOpensPTagInHtml(cmark_node *para)
{
    if (para == nullptr || cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) {
        return false;
    }
    cmark_node *parent = cmark_node_parent(para);
    if (parent == nullptr) {
        return false;
    }
    cmark_node *grandparent = cmark_node_parent(parent);
    bool tight = false;
    if (grandparent != nullptr && cmark_node_get_type(grandparent) == CMARK_NODE_LIST) {
        tight = cmark_node_get_list_tight(grandparent) != 0;
    }
    return !tight;
}

bool paragraphPreviewEditEligible(cmark_node *para)
{
    if (para == nullptr || cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) {
        return false;
    }
    cmark_node *parent = cmark_node_parent(para);
    if (parent == nullptr || cmark_node_get_type(parent) == CMARK_NODE_ITEM) {
        return false;
    }
    for (cmark_node *ch = cmark_node_first_child(para); ch != nullptr; ch = cmark_node_next(ch)) {
        const cmark_node_type ty = cmark_node_get_type(ch);
        if (ty == CMARK_NODE_TEXT || ty == CMARK_NODE_SOFTBREAK || ty == CMARK_NODE_LINEBREAK) {
            continue;
        }
        if (ty == CMARK_NODE_HTML_INLINE) {
            const char *raw = cmark_node_get_literal(ch);
            const QString lit = raw ? QString::fromUtf8(raw) : QString();
            if (!htmlInlineLiteralIsBrTagOnly(lit)) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

std::optional<std::pair<int, int>> listItemEditRangeAbs(const QString &md, cmark_node *item)
{
    if (item == nullptr || cmark_node_get_type(item) != CMARK_NODE_ITEM) {
        return std::nullopt;
    }

    cmark_node *firstPara = nullptr;
    cmark_node *lastPara = nullptr;
    bool sawListAfterPara = false;
    for (cmark_node *c = cmark_node_first_child(item); c != nullptr; c = cmark_node_next(c)) {
        const cmark_node_type t = cmark_node_get_type(c);
        if (t == CMARK_NODE_PARAGRAPH) {
            if (sawListAfterPara) {
                return std::nullopt;
            }
            if (firstPara == nullptr) {
                firstPara = c;
            }
            lastPara = c;
        } else if (t == CMARK_NODE_LIST) {
            if (lastPara != nullptr) {
                sawListAfterPara = true;
            }
        } else {
            return std::nullopt;
        }
    }

    if (firstPara == nullptr) {
        return std::nullopt;
    }

    int s = 0;
    int dummy = 0;
    if (!nodeUtf16RangeHalfOpenMultiline(md, firstPara, s, dummy)) {
        return std::nullopt;
    }
    int e = 0;
    int dummy2 = 0;
    if (!nodeUtf16RangeHalfOpenMultiline(md, lastPara, dummy2, e)) {
        return std::nullopt;
    }

    if (nodeIsTaskListItem(item)) {
        int ls = 0;
        int le = 0;
        if (lineUtf16Bounds(md, cmark_node_get_start_line(firstPara), ls, le)) {
            QStringView lv = QStringView(md).mid(ls, le - ls);
            int i = 0;
            while (i < lv.size() && (lv.at(i) == u' ' || lv.at(i) == u'\t')) {
                ++i;
            }
            const int bracketStart = ls + i;
            if (bracketStart + 3 <= md.size()
                && md.at(bracketStart) == u'['
                && md.at(bracketStart + 2) == u']') {
                int skip = bracketStart + 3;
                while (skip < md.size() && (md.at(skip) == u' ' || md.at(skip) == u'\t')) {
                    ++skip;
                }
                if (skip < e) {
                    s = skip;
                }
            }
        }
    }

    if (s >= e) {
        return std::nullopt;
    }
    return std::make_pair(s, e);
}

std::optional<int> taskListCheckboxOffsetAbs(const QString &md, cmark_node *item)
{
    if (!nodeIsTaskListItem(item)) {
        return std::nullopt;
    }
    cmark_node *para = nullptr;
    for (cmark_node *c = cmark_node_first_child(item); c != nullptr; c = cmark_node_next(c)) {
        if (cmark_node_get_type(c) == CMARK_NODE_PARAGRAPH) {
            para = c;
            break;
        }
    }
    if (para == nullptr) {
        return std::nullopt;
    }
    const int line1 = cmark_node_get_start_line(para);
    int ls = 0;
    int le = 0;
    if (!lineUtf16Bounds(md, line1, ls, le)) {
        return std::nullopt;
    }
    QStringView lv = QStringView(md).mid(ls, le - ls);
    int i = 0;
    while (i < lv.size() && (lv.at(i) == u' ' || lv.at(i) == u'\t')) {
        ++i;
    }
    if (i + 2 >= lv.size() || lv.at(i) != u'[' || lv.at(i + 2) != u']') {
        return std::nullopt;
    }
    return ls + i + 1;
}

bool parseAtxHeadingContentRange(QStringView v, int level, int &outRelStart, int &outRelEnd)
{
    int i = 0;
    const int n = v.size();
    while (i < n && i < 3 && v[i] == u' ') {
        ++i;
    }
    int hashes = 0;
    while (i < n && v[i] == u'#' && hashes < 6) {
        ++hashes;
        ++i;
    }
    if (hashes == 0 || hashes != level) {
        return false;
    }
    if (i < n && v[i] == u' ') {
        ++i;
    }
    outRelStart = i;
    outRelEnd = n;
    while (outRelEnd > outRelStart && v[outRelEnd - 1].isSpace()) {
        --outRelEnd;
    }
    return outRelStart <= outRelEnd;
}

std::optional<std::pair<int, int>> atxHeadingTextRangeAbs(const QString &md, int line1, int level)
{
    int ls = 0;
    int le = 0;
    if (!lineUtf16Bounds(md, line1, ls, le)) {
        return std::nullopt;
    }
    QStringView lv = QStringView(md).mid(ls, le - ls);
    int rs = 0;
    int re = 0;
    if (!parseAtxHeadingContentRange(lv, level, rs, re)) {
        return std::nullopt;
    }
    return std::make_pair(ls + rs, ls + re);
}

bool setextUnderlineLineLevel(QStringView line, int *outLevel)
{
    if (outLevel == nullptr) {
        return false;
    }
    int i = 0;
    const int n = line.size();
    for (int s = 0; s < 3 && i < n && line[i] == u' '; ++s) {
        ++i;
    }
    if (i >= n) {
        return false;
    }
    const QChar c = line[i];
    if (c != u'=' && c != u'-') {
        return false;
    }
    int run = 0;
    while (i < n && line[i] == c) {
        ++run;
        ++i;
    }
    if (run < 3) {
        return false;
    }
    while (i < n) {
        const QChar ch = line[i];
        if (ch != u' ' && ch != u'\t') {
            return false;
        }
        ++i;
    }
    *outLevel = (c == u'=') ? 1 : 2;
    return true;
}

std::optional<std::pair<int, int>> setextHeadingTextRangeAbs(const QString &md, int titleLine1, int level)
{
    if (level != 1 && level != 2) {
        return std::nullopt;
    }
    int uls = 0;
    int ule = 0;
    if (!lineUtf16Bounds(md, titleLine1 + 1, uls, ule)) {
        return std::nullopt;
    }
    const QStringView uline = QStringView(md).mid(uls, ule - uls);
    int ulevel = 0;
    if (!setextUnderlineLineLevel(uline, &ulevel) || ulevel != level) {
        return std::nullopt;
    }

    int ls = 0;
    int le = 0;
    if (!lineUtf16Bounds(md, titleLine1, ls, le)) {
        return std::nullopt;
    }
    const QStringView title = QStringView(md).mid(ls, le - ls);
    int rs = 0;
    int re = title.size();
    while (re > rs && title[re - 1].isSpace()) {
        --re;
    }
    if (rs >= re) {
        return std::nullopt;
    }
    return std::make_pair(ls + rs, ls + re);
}

std::optional<std::pair<int, int>> linkLabelRangeFromSlice(int absSliceStart, const QString &slice)
{
    if (!slice.startsWith(u'[')) {
        return std::nullopt;
    }
    int depth = 0;
    for (int i = 0; i < slice.size(); ++i) {
        const QChar c = slice.at(i);
        if (c == u'[') {
            ++depth;
        } else if (c == u']') {
            --depth;
            if (depth == 0) {
                const int labelStart = absSliceStart + 1;
                const int labelEndExclusive = absSliceStart + i;
                if (labelStart <= labelEndExclusive) {
                    return std::make_pair(labelStart, labelEndExclusive);
                }
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

QString escapeCmarkBodyTextToHtml(const QString &s)
{
    QString r;
    r.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'&':
            r += QStringLiteral("&amp;");
            break;
        case u'<':
            r += QStringLiteral("&lt;");
            break;
        case u'>':
            r += QStringLiteral("&gt;");
            break;
        case u'"':
            r += QStringLiteral("&quot;");
            break;
        default:
            r += c;
            break;
        }
    }
    return r;
}

bool nodeIsStrikethrough(cmark_node *node)
{
    const char *ts = cmark_node_get_type_string(node);
    return ts != nullptr && !std::strcmp(ts, "strikethrough");
}

bool nodeIsTableCell(cmark_node *node)
{
    const char *ts = cmark_node_get_type_string(node);
    return ts != nullptr && !std::strcmp(ts, "table_cell");
}

bool tableCellPreviewEditEligible(cmark_node *cell)
{
    if (cell == nullptr || !nodeIsTableCell(cell)) {
        return false;
    }
    for (cmark_node *ch = cmark_node_first_child(cell); ch != nullptr; ch = cmark_node_next(ch)) {
        const cmark_node_type ty = cmark_node_get_type(ch);
        if (ty == CMARK_NODE_TEXT || ty == CMARK_NODE_SOFTBREAK || ty == CMARK_NODE_LINEBREAK) {
            continue;
        }
        if (ty == CMARK_NODE_HTML_INLINE) {
            const char *raw = cmark_node_get_literal(ch);
            const QString lit = raw ? QString::fromUtf8(raw) : QString();
            if (!htmlInlineLiteralIsBrTagOnly(lit)) {
                return false;
            }
            continue;
        }
        if (nodeIsStrikethrough(ch)) {
            return false;
        }
        return false;
    }
    return true;
}

bool ancestorSkipsPlainTextEdit(cmark_node *textNode)
{
    for (cmark_node *a = cmark_node_parent(textNode); a != nullptr; a = cmark_node_parent(a)) {
        const cmark_node_type t = cmark_node_get_type(a);
        switch (t) {
        case CMARK_NODE_PARAGRAPH:
            if (paragraphPreviewEditEligible(a) && paragraphNodeOpensPTagInHtml(a)) {
                return true;
            }
            break;
        case CMARK_NODE_HEADING:
        case CMARK_NODE_LINK:
        case CMARK_NODE_IMAGE:
        case CMARK_NODE_CODE:
        case CMARK_NODE_EMPH:
        case CMARK_NODE_STRONG:
        case CMARK_NODE_HTML_INLINE:
        case CMARK_NODE_FOOTNOTE_REFERENCE:
        case CMARK_NODE_CUSTOM_BLOCK:
        case CMARK_NODE_CUSTOM_INLINE:
        case CMARK_NODE_ITEM:
            return true;
        default: {
            if (nodeIsTableCell(a) && tableCellPreviewEditEligible(a)) {
                return true;
            }
            if (nodeIsStrikethrough(a)) {
                return true;
            }
            break;
        }
        }
    }
    return false;
}

bool inlineFormatNodeIsOnlyDirectTextChildren(cmark_node *node)
{
    cmark_node *ch = cmark_node_first_child(node);
    if (ch == nullptr) {
        return false;
    }
    for (; ch != nullptr; ch = cmark_node_next(ch)) {
        if (cmark_node_get_type(ch) != CMARK_NODE_TEXT) {
            return false;
        }
    }
    return true;
}

std::optional<std::pair<int, int>> emphOrStrongInnerRangeAbs(const QString &md, cmark_node *node)
{
    const cmark_node_type t = cmark_node_get_type(node);
    int absStart = 0;
    int absEndEx = 0;
    if (!nodeUtf16RangeHalfOpen(md, node, absStart, absEndEx)) {
        return std::nullopt;
    }
    const QStringView slice = QStringView(md).mid(absStart, absEndEx - absStart);
    if (t == CMARK_NODE_STRONG) {
        if (slice.size() >= 4 && slice.startsWith(QStringLiteral("**")) && slice.endsWith(QStringLiteral("**"))) {
            return std::make_pair(absStart + 2, absEndEx - 2);
        }
        if (slice.size() >= 4 && slice.startsWith(QStringLiteral("__")) && slice.endsWith(QStringLiteral("__"))) {
            return std::make_pair(absStart + 2, absEndEx - 2);
        }
        return std::nullopt;
    }
    if (t == CMARK_NODE_EMPH) {
        if (slice.size() >= 2 && slice.startsWith(QStringLiteral("**"))) {
            return std::nullopt;
        }
        if (slice.size() >= 2 && slice.startsWith(u'*') && slice.endsWith(u'*')) {
            return std::make_pair(absStart + 1, absEndEx - 1);
        }
        if (slice.size() >= 2 && slice.startsWith(u'_') && slice.endsWith(u'_')) {
            return std::make_pair(absStart + 1, absEndEx - 1);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> strikethroughInnerRangeAbs(const QString &md, cmark_node *node)
{
    int absStart = 0;
    int absEndEx = 0;
    if (!nodeUtf16RangeHalfOpen(md, node, absStart, absEndEx)) {
        return std::nullopt;
    }
    const QStringView slice = QStringView(md).mid(absStart, absEndEx - absStart);
    int lead = 0;
    while (lead < slice.size() && slice.at(lead) == u'~') {
        ++lead;
    }
    int trail = 0;
    while (trail < slice.size() - lead && slice.at(slice.size() - 1 - trail) == u'~') {
        ++trail;
    }
    if (lead == 0 || lead != trail || lead > 2) {
        return std::nullopt;
    }
    const int innerStart = absStart + lead;
    const int innerEnd = absEndEx - trail;
    if (innerStart >= innerEnd) {
        return std::nullopt;
    }
    return std::make_pair(innerStart, innerEnd);
}

bool syncPastEscapedTextLiteral(const QString &html, const QString &lit, qsizetype &searchFrom)
{
    if (lit.isEmpty()) {
        return true;
    }
    const QString esc = escapeCmarkBodyTextToHtml(lit);
    const qsizetype p = html.indexOf(esc, searchFrom);
    if (p < 0) {
        return false;
    }
    searchFrom = p + esc.length();
    return true;
}

struct PlainTextWrap {
    qsizetype pos;
    qsizetype escapedLen;
    int textStart;
    int textEndExclusive;
};

QString wrapUnformattedTextInHtml(const QString &markdown, cmark_node *root, QString html)
{
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev;
    qsizetype searchFrom = 0;
    std::vector<PlainTextWrap> pending;
    bool scanOk = true;

    while (scanOk && (ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) {
            continue;
        }
        cmark_node *node = cmark_iter_get_node(iter);
        if (cmark_node_get_type(node) != CMARK_NODE_TEXT) {
            continue;
        }
        if (ancestorSkipsPlainTextEdit(node)) {
            const char *raw = cmark_node_get_literal(node);
            const QString lit = raw ? QString::fromUtf8(raw) : QString();
            if (lit.isEmpty()) {
                continue;
            }
            if (!syncPastEscapedTextLiteral(html, lit, searchFrom)) {
                scanOk = false;
                break;
            }
            continue;
        }
        int absStart = 0;
        int absEndEx = 0;
        if (!nodeUtf16RangeHalfOpen(markdown, node, absStart, absEndEx)) {
            const char *raw2 = cmark_node_get_literal(node);
            const QString lit2 = raw2 ? QString::fromUtf8(raw2) : QString();
            if (lit2.isEmpty()) {
                continue;
            }
            if (!syncPastEscapedTextLiteral(html, lit2, searchFrom)) {
                scanOk = false;
                break;
            }
            continue;
        }
        if (absStart >= absEndEx) {
            const char *rEmpty = cmark_node_get_literal(node);
            if (!rEmpty) {
                continue;
            }
            const QString lit0 = QString::fromUtf8(rEmpty);
            if (lit0.isEmpty()) {
                continue;
            }
            if (!syncPastEscapedTextLiteral(html, lit0, searchFrom)) {
                scanOk = false;
                break;
            }
            continue;
        }
        const char *rawLit = cmark_node_get_literal(node);
        if (!rawLit) {
            continue;
        }
        const QString lit = QString::fromUtf8(rawLit);
        if (lit.isEmpty()) {
            continue;
        }
        const QString esc = escapeCmarkBodyTextToHtml(lit);
        const qsizetype pos = html.indexOf(esc, searchFrom);
        if (pos < 0) {
            scanOk = false;
            break;
        }
        pending.push_back(PlainTextWrap{pos, esc.length(), absStart, absEndEx});
        searchFrom = pos + esc.length();
    }

    cmark_iter_free(iter);

    if (!scanOk) {
        return html;
    }

    std::sort(
        pending.begin(), pending.end(), [](const PlainTextWrap &a, const PlainTextWrap &b) {
            return a.pos > b.pos;
        });

    for (const PlainTextWrap &w : pending) {
        const QString open =
            QStringLiteral("<span data-gw-edit-kind=\"text\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\">")
                .arg(w.textStart)
                .arg(w.textEndExclusive);
        html.insert(w.pos + w.escapedLen, QStringLiteral("</span>"));
        html.insert(w.pos, open);
    }
    return html;
}

QString normalizeInlineCodeRawForSerialize(const QString &raw)
{
    QString t;
    t.reserve(raw.size() + 4);
    bool containsNonSpace = false;
    for (int r = 0; r < raw.size(); ++r) {
        const QChar ch = raw.at(r);
        if (ch == u'\r') {
            if (r + 1 < raw.size() && raw.at(r + 1) == u'\n') {
                ++r;
            }
            t.append(u' ');
            continue;
        }
        if (ch == u'\n') {
            t.append(u' ');
            continue;
        }
        t.append(ch);
        if (ch != u' ') {
            containsNonSpace = true;
        }
    }
    if (containsNonSpace && t.size() >= 2 && t.front() == u' ' && t.back() == u' ') {
        t = t.mid(1, t.size() - 2);
    }
    return t;
}

std::optional<std::pair<int, int>> fencedCodeBlockBodyRangeUtf16(const QString &md, cmark_node *block)
{
    if (block == nullptr || cmark_node_get_type(block) != CMARK_NODE_CODE_BLOCK) {
        return std::nullopt;
    }
    int fl = 0;
    int fo = 0;
    char fc = 0;
    if (!cmark_node_get_fenced(block, &fl, &fo, &fc)) {
        return std::nullopt;
    }
    (void)fl;
    (void)fo;
    (void)fc;

    const int sl = cmark_node_get_start_line(block);
    const int el = cmark_node_get_end_line(block);
    if (sl >= el) {
        return std::nullopt;
    }
    int openLs = 0;
    int openLe = 0;
    if (!lineUtf16Bounds(md, sl, openLs, openLe)) {
        return std::nullopt;
    }
    int bodyStart = openLe;
    if (bodyStart < md.size() && md.at(bodyStart) == u'\r') {
        ++bodyStart;
    }
    if (bodyStart < md.size() && md.at(bodyStart) == u'\n') {
        ++bodyStart;
    }
    int closeLs = 0;
    int closeLineEnd = 0;
    if (!lineUtf16Bounds(md, el, closeLs, closeLineEnd)) {
        return std::nullopt;
    }
    (void)closeLineEnd;
    const int bodyEndExclusive = closeLs;
    if (bodyStart > bodyEndExclusive || bodyStart < 0 || bodyEndExclusive > md.size()) {
        return std::nullopt;
    }
    const char *litRaw = cmark_node_get_literal(block);
    const QString lit = litRaw ? QString::fromUtf8(litRaw) : QString();
    QString body = md.mid(bodyStart, bodyEndExclusive - bodyStart);
    body.replace(QStringLiteral("\r\n"), QChar(u'\n'));
    body.replace(u'\r', u'\n');
    if (body != lit) {
        return std::nullopt;
    }
    return std::make_pair(bodyStart, bodyEndExclusive);
}

bool htmlIndexInsideOpenPre(const QString &html, qsizetype idx)
{
    int depth = 0;
    qsizetype i = 0;
    while (i < idx && i < html.size()) {
        const qsizetype openPos = html.indexOf(QLatin1String("<pre"), i);
        const qsizetype closePos = html.indexOf(QLatin1String("</pre>"), i);
        const bool haveOpen = openPos >= 0;
        const bool haveClose = closePos >= 0;
        qsizetype next = -1;
        bool isOpen = false;
        if (haveOpen && (!haveClose || openPos < closePos)) {
            next = openPos;
            isOpen = true;
        } else if (haveClose) {
            next = closePos;
            isOpen = false;
        } else {
            break;
        }
        if (next >= idx) {
            break;
        }
        if (isOpen) {
            ++depth;
            i = next + 4;
        } else {
            depth = std::max(0, depth - 1);
            i = next + 6;
        }
    }
    return depth > 0;
}

struct GtInjection {
    qsizetype gtPos = 0;
    QString inj;
};

struct LinkInj {
    QString url;
    QString inj;
};

struct HeadingInj {
    int level = 1;
    QString inj;
};

using HtmlBlockRange = std::pair<qsizetype, qsizetype>;

bool posInsideHtmlBlock(qsizetype pos, const std::vector<HtmlBlockRange> &ranges)
{
    for (const HtmlBlockRange &r : ranges) {
        if (pos >= r.first && pos < r.second) {
            return true;
        }
    }
    return false;
}

std::vector<HtmlBlockRange> computeHtmlBlockRanges(const QString &html, const std::vector<QString> &blockLiterals)
{
    std::vector<HtmlBlockRange> out;
    out.reserve(blockLiterals.size());
    qsizetype cursor = 0;
    for (const QString &lit : blockLiterals) {
        if (lit.isEmpty()) {
            continue;
        }
        const qsizetype p = html.indexOf(lit, cursor);
        if (p < 0) {
            continue;
        }
        const qsizetype e = p + lit.size();
        out.push_back({p, e});
        cursor = e;
    }
    return out;
}

QString minimalHtmlEntityUnescape(const QString &s)
{
    QString r = s;
    r.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    r.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    r.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    r.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    r.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    r.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    return r;
}

void sortGtInjectionsDescendingAndApply(QString &html, std::vector<GtInjection> &items)
{
    std::sort(items.begin(), items.end(), [](const GtInjection &a, const GtInjection &b) {
        return a.gtPos > b.gtPos;
    });
    for (const GtInjection &it : items) {
        html.insert(it.gtPos, it.inj);
    }
}

void injectInlineCodeBeforeGt(QString &html, const std::vector<QString> &injections,
                              const std::vector<HtmlBlockRange> *skipRanges = nullptr)
{
    std::vector<GtInjection> items;
    items.reserve(injections.size());
    qsizetype searchFrom = 0;
    for (const QString &inj : injections) {
        for (;;) {
            const qsizetype p = html.indexOf(QLatin1String("<code"), searchFrom);
            if (p < 0) {
                return;
            }
            const qsizetype gt = html.indexOf(u'>', p);
            if (gt < 0) {
                return;
            }
            if (htmlIndexInsideOpenPre(html, p)) {
                searchFrom = p + 5;
                continue;
            }
            if (skipRanges != nullptr && posInsideHtmlBlock(p, *skipRanges)) {
                searchFrom = gt + 1;
                continue;
            }
            items.push_back({gt, inj});
            searchFrom = gt + 1;
            break;
        }
    }
    sortGtInjectionsDescendingAndApply(html, items);
}

void injectBeforeGt(QString &html, const QRegularExpression &re, const std::vector<QString> &injections,
                    const std::vector<HtmlBlockRange> *skipRanges = nullptr)
{
    std::vector<GtInjection> items;
    items.reserve(injections.size());

    qsizetype searchPos = 0;
    for (const QString &inj : injections) {
        QRegularExpressionMatch m = re.match(html, searchPos);
        while (m.hasMatch() && skipRanges != nullptr
               && posInsideHtmlBlock(m.capturedStart(0), *skipRanges)) {
            searchPos = m.capturedEnd(0);
            m = re.match(html, searchPos);
        }
        if (!m.hasMatch()) {
            break;
        }
        if (!inj.isEmpty()) {
            items.push_back({m.capturedEnd(0) - 1, inj});
        }
        searchPos = m.capturedEnd(0);
    }

    sortGtInjectionsDescendingAndApply(html, items);
}

void injectLinksByHrefMatch(QString &html, const std::vector<LinkInj> &injections,
                            const std::vector<HtmlBlockRange> *skipRanges = nullptr)
{
    static const QRegularExpression linkRe(
        QStringLiteral("<a\\s+href\\s*=\\s*\"([^\"]*)\"[^>]*>"));
    std::vector<GtInjection> items;
    items.reserve(injections.size());

    qsizetype searchPos = 0;
    for (const LinkInj &li : injections) {
        while (true) {
            const QRegularExpressionMatch m = linkRe.match(html, searchPos);
            if (!m.hasMatch()) {
                break;
            }
            const qsizetype start = m.capturedStart(0);
            searchPos = m.capturedEnd(0);
            if (skipRanges != nullptr && posInsideHtmlBlock(start, *skipRanges)) {
                continue;
            }
            const QString href = minimalHtmlEntityUnescape(m.captured(1));
            if (href != li.url) {
                continue;
            }
            if (!li.inj.isEmpty()) {
                items.push_back({searchPos - 1, li.inj});
            }
            break;
        }
    }

    sortGtInjectionsDescendingAndApply(html, items);
}

void injectHeadingsByLevelMatch(QString &html, const std::vector<HeadingInj> &injections,
                                const std::vector<HtmlBlockRange> *skipRanges = nullptr)
{
    std::vector<GtInjection> items;
    items.reserve(injections.size());
    qsizetype searchPos = 0;
    for (const HeadingInj &hi : injections) {
        if (hi.level < 1 || hi.level > 6) {
            break;
        }
        const QRegularExpression re(QStringLiteral("<h%1\\b[^>]*>").arg(hi.level));
        QRegularExpressionMatch m = re.match(html, searchPos);
        while (m.hasMatch() && skipRanges != nullptr
               && posInsideHtmlBlock(m.capturedStart(0), *skipRanges)) {
            searchPos = m.capturedEnd(0);
            m = re.match(html, searchPos);
        }
        if (!m.hasMatch()) {
            break;
        }
        if (!hi.inj.isEmpty()) {
            items.push_back({m.capturedEnd(0) - 1, hi.inj});
        }
        searchPos = m.capturedEnd(0);
    }
    sortGtInjectionsDescendingAndApply(html, items);
}

void injectNestedListLocksBeforeGt(QString &html, const std::vector<QString> &injections)
{
    if (injections.empty()) {
        return;
    }

    static const QRegularExpression tagRe(QStringLiteral("<(/?)(ul|ol|li)\\b[^>]*>"));
    std::vector<qsizetype> nestedOpenGtPositions;
    int liDepth = 0;

    for (qsizetype pos = 0; pos < html.size();) {
        const QRegularExpressionMatch m = tagRe.match(html, pos);
        if (!m.hasMatch()) {
            break;
        }
        const bool closing = (m.capturedView(1) == QLatin1String("/"));
        const QStringView name = m.capturedView(2);
        const qsizetype gt = m.capturedEnd(0) - 1;

        if (name == QLatin1String("li")) {
            if (closing) {
                if (liDepth > 0) {
                    --liDepth;
                }
            } else {
                ++liDepth;
            }
        } else if (!closing && liDepth > 0) {
            nestedOpenGtPositions.push_back(gt);
        }
        pos = m.capturedEnd(0);
    }

    const size_t n = std::min(injections.size(), nestedOpenGtPositions.size());
    std::vector<GtInjection> items;
    items.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!injections[i].isEmpty()) {
            items.push_back({nestedOpenGtPositions[i], injections[i]});
        }
    }
    sortGtInjectionsDescendingAndApply(html, items);
}

void injectBeforeSelfCloseOrGt(QString &html, const QRegularExpression &re, const std::vector<QString> &injections)
{
    std::vector<GtInjection> items;
    items.reserve(injections.size());

    qsizetype searchPos = 0;
    for (const QString &inj : injections) {
        const QRegularExpressionMatch m = re.match(html, searchPos);
        if (!m.hasMatch()) {
            break;
        }
        if (!inj.isEmpty()) {
            qsizetype pos = m.capturedEnd(0) - 1;
            if (pos > 0 && html.at(pos - 1) == u'/') {
                --pos;
            }
            items.push_back({pos, inj});
        }
        searchPos = m.capturedEnd(0);
    }

    sortGtInjectionsDescendingAndApply(html, items);
}

} // namespace

QString augmentPreviewHtmlWithEditMetadata(const QString &markdown, cmark_node *root, QString html)
{
    html = wrapUnformattedTextInHtml(markdown, root, std::move(html));

    std::vector<HeadingInj> headingInj;
    std::vector<LinkInj> linkInj;
    std::vector<QString> emphInj;
    std::vector<QString> strongInj;
    std::vector<QString> strikeInj;
    std::vector<QString> codeBlockInj;
    std::vector<QString> codespanInj;
    std::vector<QString> paragraphInj;
    std::vector<QString> listItemInj;
    std::vector<QString> nestedListLockInj;
    std::vector<QString> taskCheckboxInj;
    std::vector<QString> hrInj;
    std::vector<QString> tableCellInj;
    std::vector<QString> htmlBlockLiterals;

    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev;
    // CMARK_NODE_HTML_BLOCK is not preview-editable; we still track its literal to mask out any
    // raw <a>/<em>/etc. tags inside it so they don't steal injections from real cmark nodes.

    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) {
            continue;
        }

        cmark_node *node = cmark_iter_get_node(iter);
        const cmark_node_type t = cmark_node_get_type(node);

        if (t == CMARK_NODE_HEADING) {
            const int level = cmark_node_get_heading_level(node);
            const int line1 = cmark_node_get_start_line(node);
            std::optional<std::pair<int, int>> range = atxHeadingTextRangeAbs(markdown, line1, level);
            if (!range) {
                range = setextHeadingTextRangeAbs(markdown, line1, level);
            }
            if (!range) {
                continue;
            }
            const int s = range->first;
            const int e = range->second;
            if (s >= e) {
                continue;
            }
            headingInj.push_back(
                {level,
                 QStringLiteral(" data-gw-edit-kind=\"heading\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" data-gw-level=\"%3\" contenteditable=\"true\"")
                     .arg(s)
                     .arg(e)
                     .arg(level)});
        } else if (t == CMARK_NODE_LINK) {
            const char *url = cmark_node_get_url(node);
            if (!url) {
                continue;
            }
            const QString urlStr = QString::fromUtf8(url);
            if (urlStr.startsWith(QLatin1String("#fn"))) {
                continue;
            }
            int absStart = 0;
            int absEndEx = 0;
            if (!nodeUtf16RangeHalfOpen(markdown, node, absStart, absEndEx)) {
                continue;
            }
            const QString slice = markdown.mid(absStart, absEndEx - absStart);
            const auto label = linkLabelRangeFromSlice(absStart, slice);
            if (!label) {
                continue;
            }
            if (label->first >= label->second) {
                continue;
            }
            linkInj.push_back({urlStr,
                               QStringLiteral(" data-gw-edit-kind=\"link\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                   .arg(label->first)
                                   .arg(label->second)});
        } else if (t == CMARK_NODE_EMPH) {
            if (!inlineFormatNodeIsOnlyDirectTextChildren(node)) {
                continue;
            }
            const auto range = emphOrStrongInnerRangeAbs(markdown, node);
            if (!range || range->first >= range->second) {
                continue;
            }
            emphInj.push_back(QStringLiteral(" data-gw-edit-kind=\"emph\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                  .arg(range->first)
                                  .arg(range->second));
        } else if (t == CMARK_NODE_STRONG) {
            if (!inlineFormatNodeIsOnlyDirectTextChildren(node)) {
                continue;
            }
            const auto range = emphOrStrongInnerRangeAbs(markdown, node);
            if (!range || range->first >= range->second) {
                continue;
            }
            strongInj.push_back(QStringLiteral(" data-gw-edit-kind=\"strong\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                    .arg(range->first)
                                    .arg(range->second));
        } else if (nodeIsStrikethrough(node)) {
            if (!inlineFormatNodeIsOnlyDirectTextChildren(node)) {
                continue;
            }
            const auto range = strikethroughInnerRangeAbs(markdown, node);
            if (!range || range->first >= range->second) {
                continue;
            }
            strikeInj.push_back(QStringLiteral(" data-gw-edit-kind=\"strikethrough\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                    .arg(range->first)
                                    .arg(range->second));
        } else if (t == CMARK_NODE_CODE) {
            int absStart = 0;
            int absEndEx = 0;
            if (!nodeUtf16RangeHalfOpen(markdown, node, absStart, absEndEx) || absStart >= absEndEx) {
                continue;
            }
            codespanInj.push_back(QStringLiteral(" data-gw-edit-kind=\"codespan\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                      .arg(absStart)
                                      .arg(absEndEx));
        } else if (t == CMARK_NODE_CODE_BLOCK) {
            int fenceLen = 0;
            int fenceOff = 0;
            char fenceCh = 0;
            if (cmark_node_get_fenced(node, &fenceLen, &fenceOff, &fenceCh)) {
                const auto bodyRange = fencedCodeBlockBodyRangeUtf16(markdown, node);
                if (!bodyRange || bodyRange->first >= bodyRange->second) {
                    continue;
                }
                codeBlockInj.push_back(
                    QStringLiteral(" data-gw-edit-kind=\"codeblock\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                        .arg(bodyRange->first)
                        .arg(bodyRange->second));
            } else {
                int wbStart = 0;
                int wbEnd = 0;
                if (!nodeUtf16RangeHalfOpenMultiline(markdown, node, wbStart, wbEnd) || wbStart >= wbEnd) {
                    continue;
                }
                codeBlockInj.push_back(
                    QStringLiteral(" data-gw-edit-kind=\"codeblock\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                        .arg(wbStart)
                        .arg(wbEnd));
            }
        } else if (t == CMARK_NODE_THEMATIC_BREAK) {
            hrInj.push_back(QStringLiteral(" contenteditable=\"false\""));
        } else if (t == CMARK_NODE_HTML_BLOCK) {
            const char *raw = cmark_node_get_literal(node);
            if (raw != nullptr) {
                const QString lit = QString::fromUtf8(raw);
                if (!lit.isEmpty()) {
                    htmlBlockLiterals.push_back(lit);
                }
            }
        } else if (t == CMARK_NODE_PARAGRAPH) {
            if (!paragraphNodeOpensPTagInHtml(node)) {
                continue;
            }
            QString pInj;
            cmark_node *pParent = cmark_node_parent(node);
            if (pParent != nullptr && cmark_node_get_type(pParent) != CMARK_NODE_ITEM
                && paragraphPreviewEditEligible(node)) {
                int ps = 0;
                int pe = 0;
                if (nodeUtf16RangeHalfOpenMultiline(markdown, node, ps, pe) && ps < pe) {
                    pInj = QStringLiteral(" data-gw-edit-kind=\"paragraph\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                               .arg(ps)
                               .arg(pe);
                }
            }
            paragraphInj.push_back(std::move(pInj));
        } else if (t == CMARK_NODE_ITEM) {
            const auto range = listItemEditRangeAbs(markdown, node);
            if (!range || range->first >= range->second) {
                listItemInj.push_back(QString());
            } else {
                listItemInj.push_back(
                    QStringLiteral(" data-gw-edit-kind=\"listitem\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                        .arg(range->first)
                        .arg(range->second));
            }
            if (nodeIsTaskListItem(node)) {
                const auto off = taskListCheckboxOffsetAbs(markdown, node);
                if (off) {
                    taskCheckboxInj.push_back(
                        QStringLiteral(" data-gw-edit-kind=\"tasklist\" data-gw-checkbox-source=\"%1\"")
                            .arg(*off));
                } else {
                    taskCheckboxInj.push_back(QString());
                }
            }
        } else if (t == CMARK_NODE_LIST) {
            cmark_node *parent = cmark_node_parent(node);
            if (parent != nullptr && cmark_node_get_type(parent) == CMARK_NODE_ITEM) {
                nestedListLockInj.push_back(QStringLiteral(" contenteditable=\"false\""));
            }
        } else if (nodeIsTableCell(node)) {
            QString cellInj;
            if (tableCellPreviewEditEligible(node)) {
                int cs = 0;
                int ce = 0;
                if ((nodeUtf16RangeHalfOpen(markdown, node, cs, ce) || nodeUtf16RangeHalfOpenMultiline(markdown, node, cs, ce))
                    && cs < ce) {
                    cellInj = QStringLiteral(" data-gw-edit-kind=\"tablecell\" data-gw-text-start=\"%1\" data-gw-text-end=\"%2\" contenteditable=\"true\"")
                                  .arg(cs)
                                  .arg(ce);
                }
            }
            tableCellInj.push_back(std::move(cellInj));
        }
    }

    cmark_iter_free(iter);

    static const QRegularExpression paragraphRe(QStringLiteral("<p\\b[^>]*>"));
    static const QRegularExpression emphRe(QStringLiteral("<em\\b[^>]*>"));
    static const QRegularExpression strongRe(QStringLiteral("<strong\\b[^>]*>"));
    static const QRegularExpression delRe(QStringLiteral("<del\\b[^>]*>"));
    static const QRegularExpression preCodeRe(QStringLiteral("<pre\\b[^>]*>\\s*<code\\b[^>]*>"));
    static const QRegularExpression liRe(QStringLiteral("<li\\b[^>]*>"));
    static const QRegularExpression taskInputRe(QStringLiteral("<input\\b[^>]*type\\s*=\\s*\"checkbox\"[^>]*?/?>"));
    static const QRegularExpression hrRe(QStringLiteral("<hr\\b[^>]*>"));
    static const QRegularExpression tableCellRe(QStringLiteral("<(th|td)\\b[^>]*>"));

    auto blockRanges = [&]() { return computeHtmlBlockRanges(html, htmlBlockLiterals); };

    { const auto r = blockRanges(); injectHeadingsByLevelMatch(html, headingInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, paragraphRe, paragraphInj, &r); }
    { const auto r = blockRanges(); injectLinksByHrefMatch(html, linkInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, emphRe, emphInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, strongRe, strongInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, delRe, strikeInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, preCodeRe, codeBlockInj, &r); }
    if (!hrInj.empty()) {
        const auto r = blockRanges();
        injectBeforeGt(html, hrRe, hrInj, &r);
    }
    { const auto r = blockRanges(); injectInlineCodeBeforeGt(html, codespanInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, liRe, listItemInj, &r); }
    { const auto r = blockRanges(); injectBeforeGt(html, tableCellRe, tableCellInj, &r); }
    injectNestedListLocksBeforeGt(html, nestedListLockInj);
    injectBeforeSelfCloseOrGt(html, taskInputRe, taskCheckboxInj);

    if (!taskCheckboxInj.empty()) {
        html.replace(QLatin1String(" disabled=\"\""), QLatin1String(""));
    }

    return html;
}

PreviewEditTextMap buildPreviewEditTextMap(const QString &markdown,
                                           cmark_node *root,
                                           int elementSourceStart,
                                           int elementSourceEndExclusive,
                                           bool allowSoftbreaks,
                                           bool allowParagraphGaps,
                                           bool requirePlainTextOnly)
{
    PreviewEditTextMap out;

    if (root == nullptr) {
        return out;
    }
    if (elementSourceStart < 0 || elementSourceEndExclusive <= elementSourceStart) {
        return out;
    }

    cmark_event_type ev;
    cmark_iter *preIter = cmark_iter_new(root);
    while ((ev = cmark_iter_next(preIter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) {
            continue;
        }
        cmark_node *preNode = cmark_iter_get_node(preIter);
        const cmark_node_type preType = cmark_node_get_type(preNode);
        if (preType == CMARK_NODE_CODE) {
            int a = 0;
            int b = 0;
            if (!nodeUtf16RangeHalfOpen(markdown, preNode, a, b)) {
                continue;
            }
            if (a != elementSourceStart || b != elementSourceEndExclusive) {
                continue;
            }
            const char *rawLit = cmark_node_get_literal(preNode);
            const QString lit = rawLit ? QString::fromUtf8(rawLit) : QString();
            TextNodeSlot slot;
            slot.plainStart = 0;
            slot.plainEnd = static_cast<int>(lit.size());
            slot.sourceStart = a;
            slot.sourceEnd = b;
            out.plain = lit;
            out.nodes.push_back(slot);
            out.valid = true;
            out.hasUntrackedText = false;
            cmark_iter_free(preIter);
            return out;
        }
        if (preType == CMARK_NODE_CODE_BLOCK) {
            int fenceLen = 0;
            int fenceOff = 0;
            char fenceCh = 0;
            if (cmark_node_get_fenced(preNode, &fenceLen, &fenceOff, &fenceCh)) {
                const auto br = fencedCodeBlockBodyRangeUtf16(markdown, preNode);
                if (!br || br->first != elementSourceStart || br->second != elementSourceEndExclusive) {
                    continue;
                }
                const char *rawLit = cmark_node_get_literal(preNode);
                const QString lit = rawLit ? QString::fromUtf8(rawLit) : QString();
                TextNodeSlot slot;
                slot.plainStart = 0;
                slot.plainEnd = static_cast<int>(lit.size());
                slot.sourceStart = br->first;
                slot.sourceEnd = br->second;
                out.plain = lit;
                out.nodes.push_back(slot);
                out.valid = true;
                out.hasUntrackedText = false;
                cmark_iter_free(preIter);
                return out;
            }
            int wbStart = 0;
            int wbEnd = 0;
            if (!nodeUtf16RangeHalfOpenMultiline(markdown, preNode, wbStart, wbEnd)) {
                continue;
            }
            if (wbStart != elementSourceStart || wbEnd != elementSourceEndExclusive) {
                continue;
            }
            const char *rawLitInd = cmark_node_get_literal(preNode);
            const QString litInd = rawLitInd ? QString::fromUtf8(rawLitInd) : QString();
            out.plain = litInd;
            out.nodes.clear();
            out.valid = true;
            out.hasUntrackedText = false;
            out.codeblockWholeSourceReplace = true;
            cmark_iter_free(preIter);
            return out;
        }
    }
    cmark_iter_free(preIter);

    const int elemStartLine = lineNumberForUtf16Offset(markdown, elementSourceStart);
    const int elemEndLine = lineNumberForUtf16Offset(
        markdown, std::max(elementSourceStart, elementSourceEndExclusive - 1));

    cmark_iter *iter = cmark_iter_new(root);
    int plainPos = 0;
    bool hasUntracked = false;
    int paragraphsSeen = 0;
    int lastParagraphEndAbs = -1;
    int lastTextEndAbs = -1;

    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) {
            continue;
        }

        cmark_node *node = cmark_iter_get_node(iter);
        const cmark_node_type t = cmark_node_get_type(node);

        const int nodeStartLine = cmark_node_get_start_line(node);
        const int nodeEndLine = cmark_node_get_end_line(node);
        int effStartLine = nodeStartLine;
        int effEndLine = nodeEndLine;
        if (effStartLine <= 0 || effEndLine <= 0) {
            int as = 0;
            int ae = 0;
            if (nodeLineRangeViaAncestors(node, as, ae)) {
                effStartLine = as;
                effEndLine = ae;
            }
        }
        if (effStartLine > 0 && effEndLine > 0
            && (effEndLine < elemStartLine || effStartLine > elemEndLine)) {
            continue;
        }

        int absStart = 0;
        int absEnd = 0;
        bool hasRange = nodeUtf16RangeHalfOpen(markdown, node, absStart, absEnd);
        if (!hasRange && (t == CMARK_NODE_PARAGRAPH)) {
            hasRange = nodeUtf16RangeHalfOpenMultiline(markdown, node, absStart, absEnd);
        }
        if (!hasRange && nodeIsTableCell(node)) {
            hasRange = nodeUtf16RangeHalfOpenMultiline(markdown, node, absStart, absEnd);
        }

        if (hasRange) {
            if (absEnd <= elementSourceStart || absStart >= elementSourceEndExclusive) {
                continue;
            }
        }

        if (t == CMARK_NODE_TEXT) {
            if (!hasRange) {
                continue;
            }
            if (absStart < elementSourceStart || absEnd > elementSourceEndExclusive) {
                continue;
            }

            bool imageAncestor = false;
            for (cmark_node *a = cmark_node_parent(node); a != nullptr; a = cmark_node_parent(a)) {
                if (cmark_node_get_type(a) == CMARK_NODE_IMAGE) {
                    imageAncestor = true;
                    break;
                }
            }
            if (imageAncestor) {
                continue;
            }

            const char *raw = cmark_node_get_literal(node);
            if (!raw) {
                continue;
            }
            const QString lit = QString::fromUtf8(raw);
            if (lit.isEmpty()) {
                continue;
            }

            TextNodeSlot slot;
            slot.plainStart = plainPos;
            slot.plainEnd = plainPos + static_cast<int>(lit.size());
            slot.sourceStart = absStart;
            slot.sourceEnd = absEnd;
            out.nodes.push_back(slot);
            out.plain += lit;
            plainPos += static_cast<int>(lit.size());
            lastTextEndAbs = absEnd;
        } else if (t == CMARK_NODE_CODE) {
            int a = 0;
            int b = 0;
            if (nodeUtf16RangeHalfOpen(markdown, node, a, b)) {
                if (a == elementSourceStart && b == elementSourceEndExclusive) {
                    continue;
                }
                if (b <= elementSourceStart || a >= elementSourceEndExclusive) {
                    continue;
                }
                hasUntracked = true;
            } else {
                hasUntracked = true;
            }
        } else if (t == CMARK_NODE_CODE_BLOCK) {
            int fenceLen = 0;
            int fenceOff = 0;
            char fenceCh = 0;
            if (cmark_node_get_fenced(node, &fenceLen, &fenceOff, &fenceCh)) {
                const auto br = fencedCodeBlockBodyRangeUtf16(markdown, node);
                if (br) {
                    const int a = br->first;
                    const int b = br->second;
                    if (a == elementSourceStart && b == elementSourceEndExclusive) {
                        continue;
                    }
                    if (b <= elementSourceStart || a >= elementSourceEndExclusive) {
                        continue;
                    }
                    hasUntracked = true;
                }
            } else {
                int ws = 0;
                int we = 0;
                if (nodeUtf16RangeHalfOpenMultiline(markdown, node, ws, we)) {
                    if (ws == elementSourceStart && we == elementSourceEndExclusive) {
                        continue;
                    }
                    if (we <= elementSourceStart || ws >= elementSourceEndExclusive) {
                        continue;
                    }
                    hasUntracked = true;
                }
            }
        } else if (t == CMARK_NODE_SOFTBREAK || t == CMARK_NODE_LINEBREAK) {
            if (!allowSoftbreaks) {
                if (hasRange
                    && absStart >= elementSourceStart
                    && absEnd <= elementSourceEndExclusive) {
                    hasUntracked = true;
                } else if (!hasRange) {
                    hasUntracked = true;
                }
            } else {
                int bs = 0;
                int be = 0;
                if (!cmarkInlineSourceRangeUtf16(markdown, node, bs, be)) {
                    hasUntracked = true;
                    continue;
                }
                if (!utf16RangeInsideElement(bs, be, elementSourceStart, elementSourceEndExclusive)) {
                    hasUntracked = true;
                    continue;
                }
                pushPlainNewlineSlot(out.nodes, out.plain, plainPos, lastTextEndAbs, bs, be);
            }
        } else if (t == CMARK_NODE_HTML_INLINE) {
            int hs = 0;
            int he = 0;
            if (!cmarkInlineSourceRangeUtf16(markdown, node, hs, he)) {
                hasUntracked = true;
                continue;
            }
            if (he <= elementSourceStart || hs >= elementSourceEndExclusive) {
                continue;
            }
            if (!utf16RangeInsideElement(hs, he, elementSourceStart, elementSourceEndExclusive)) {
                hasUntracked = true;
                continue;
            }
            const char *rawH = cmark_node_get_literal(node);
            const QString litH = rawH ? QString::fromUtf8(rawH) : QString();
            if (!allowSoftbreaks) {
                hasUntracked = true;
                continue;
            }
            if (htmlInlineLiteralIsBrTagOnly(litH)) {
                pushPlainNewlineSlot(out.nodes, out.plain, plainPos, lastTextEndAbs, hs, he);
            } else {
                hasUntracked = true;
            }
        } else if (t == CMARK_NODE_PARAGRAPH) {
            if (hasRange
                && absStart < elementSourceEndExclusive
                && absEnd > elementSourceStart) {
                if (allowParagraphGaps && paragraphsSeen > 0 && lastParagraphEndAbs >= 0) {
                    const int gapStart = lastParagraphEndAbs;
                    const int gapEnd = absStart;
                    if (gapStart >= elementSourceStart
                        && gapEnd <= elementSourceEndExclusive
                        && gapStart < gapEnd) {
                        TextNodeSlot slot;
                        slot.plainStart = plainPos;
                        slot.plainEnd = plainPos + 2;
                        slot.sourceStart = gapStart;
                        slot.sourceEnd = gapEnd;
                        out.nodes.push_back(slot);
                        out.plain += QStringLiteral("\n\n");
                        plainPos += 2;
                        lastTextEndAbs = gapEnd;
                    }
                }
                paragraphsSeen += 1;
                lastParagraphEndAbs = absEnd;
            }
        }

        if (requirePlainTextOnly) {
            switch (t) {
            case CMARK_NODE_EMPH:
            case CMARK_NODE_STRONG:
            case CMARK_NODE_LINK:
            case CMARK_NODE_IMAGE:
            case CMARK_NODE_FOOTNOTE_REFERENCE:
            case CMARK_NODE_CUSTOM_INLINE:
                setUntrackedIfInlineOverlaps(markdown, node, elementSourceStart, elementSourceEndExclusive, hasUntracked);
                break;
            default:
                if (nodeIsStrikethrough(node)) {
                    setUntrackedIfInlineOverlaps(markdown, node, elementSourceStart, elementSourceEndExclusive, hasUntracked);
                }
                break;
            }
        }
    }

    cmark_iter_free(iter);

    out.hasUntrackedText = hasUntracked;
    out.valid = !out.nodes.empty() && !hasUntracked;
    return out;
}

int longestBacktickRun(const QString &s)
{
    int best = 0;
    int cur = 0;
    for (const QChar c : s) {
        if (c == u'`') {
            ++cur;
            best = std::max(best, cur);
        } else {
            cur = 0;
        }
    }
    return best;
}

QString serializeInlineCodeRawInner(const QString &normalizedPlain, int openFenceLen)
{
    if (openFenceLen < 1) {
        return QString();
    }
    if (longestBacktickRun(normalizedPlain) >= openFenceLen) {
        return QString();
    }
    QString candidate = normalizedPlain;
    if (normalizeInlineCodeRawForSerialize(candidate) == normalizedPlain) {
        return candidate;
    }
    candidate = QStringLiteral(" ") + normalizedPlain + QStringLiteral(" ");
    if (normalizeInlineCodeRawForSerialize(candidate) == normalizedPlain) {
        return candidate;
    }
    candidate = QStringLiteral("  ") + normalizedPlain + QStringLiteral("  ");
    if (normalizeInlineCodeRawForSerialize(candidate) == normalizedPlain) {
        return candidate;
    }
    return QString();
}

bool previewListItemTextHasListStarter(const QString &text)
{
    const QStringList lines = text.split(u'\n');
    for (const QString &line : lines) {
        int i = 0;
        const int n = line.size();
        while (i < n && (line.at(i) == u' ' || line.at(i) == u'\t')) {
            ++i;
        }
        const int rem = n - i;
        if (rem >= 2) {
            const QChar c0 = line.at(i);
            const QChar c1 = line.at(i + 1);
            if ((c0 == u'-' || c0 == u'*' || c0 == u'+') && (c1 == u' ' || c1 == u'\t')) {
                return true;
            }
        }
        int j = i;
        while (j < n && line.at(j).isDigit()) {
            ++j;
        }
        if (j > i && j < n) {
            const QChar d = line.at(j);
            if (d == u'.' || d == u')') {
                if (j + 1 < n && (line.at(j + 1) == u' ' || line.at(j + 1) == u'\t')) {
                    return true;
                }
                if (j + 1 == n) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool previewCodeblockTextHasFenceLikeLine(const QString &text)
{
    const QStringList lines = text.split(u'\n');
    for (const QString &line : lines) {
        QStringView v = QStringView(line).trimmed();
        if (v.isEmpty()) {
            continue;
        }
        int i = 0;
        while (i < 3 && i < v.size() && v.at(i) == u' ') {
            ++i;
        }
        const QStringView rest = v.mid(i);
        if (rest.startsWith(QLatin1String("```")) || rest.startsWith(QLatin1String("~~~"))) {
            return true;
        }
    }
    return false;
}

QString serializeIndentedCodeBlock(const QString &literalPlain)
{
    QString norm = literalPlain;
    norm.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    norm.replace(u'\r', u'\n');

    if (norm.isEmpty()) {
        return QStringLiteral("    \n");
    }

    QString out;
    int lineStart = 0;
    const int n = norm.size();
    for (int i = 0; i <= n; ++i) {
        if (i == n || norm.at(i) == u'\n') {
            const QStringView line = QStringView(norm).mid(lineStart, i - lineStart);
            if (line.isEmpty()) {
                if (i < n) {
                    out += QLatin1Char('\n');
                }
            } else {
                out += QStringLiteral("    ");
                out += line;
                if (i < n) {
                    out += QLatin1Char('\n');
                }
            }
            lineStart = i + 1;
        }
    }
    if (!out.endsWith(QLatin1Char('\n'))) {
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace ghostwriterpp
