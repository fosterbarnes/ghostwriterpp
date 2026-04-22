/*
 * SPDX-FileCopyrightText: 2018-2023 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "previewproxy.h"

#include "htmlpreview.h"

namespace ghostwriterpp
{
PreviewProxy::PreviewProxy(QObject *parent)
    : QObject(parent),
      m_htmlContent(""),
      m_styleSheet(""),
      m_mathEnabled(false)
{
    ;
}

PreviewProxy::~PreviewProxy()
{
    ;
}

void PreviewProxy::setHtmlContent(const QString &html)
{
    m_htmlContent = html;
    emit htmlChanged(m_htmlContent);
}

QString PreviewProxy::htmlContent() const
{
    return m_htmlContent;
}

void PreviewProxy::setStyleSheet(const QString &css)
{
    m_styleSheet = css;
    emit styleSheetChanged(m_styleSheet);
}

QString PreviewProxy::styleSheet() const
{
    return m_styleSheet;
}

void PreviewProxy::setMathEnabled(bool enabled)
{
    if (enabled != m_mathEnabled) {
        m_mathEnabled = enabled;
        emit mathToggled(m_mathEnabled);
    }
}

bool PreviewProxy::mathEnabled() const
{
    return m_mathEnabled;
}

void PreviewProxy::beginPreviewEdit(const QString &kind, int start, int end)
{
    HtmlPreview *hp = qobject_cast<HtmlPreview *>(parent());
    if (hp) {
        hp->beginPreviewEditSession(kind, start, end);
    }
}

void PreviewProxy::applyPreviewEdit(const QString &text)
{
    HtmlPreview *hp = qobject_cast<HtmlPreview *>(parent());
    if (hp) {
        hp->applyPreviewEdit(text);
    }
}

void PreviewProxy::endPreviewEdit()
{
    HtmlPreview *hp = qobject_cast<HtmlPreview *>(parent());
    if (hp) {
        hp->endPreviewEditSession();
    }
}

void PreviewProxy::togglePreviewCheckbox(int offset, bool checked)
{
    HtmlPreview *hp = qobject_cast<HtmlPreview *>(parent());
    if (hp) {
        hp->togglePreviewCheckbox(offset, checked);
    }
}
} // namespace ghostwriterpp
