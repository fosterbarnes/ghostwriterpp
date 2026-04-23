/*
 * SPDX-FileCopyrightText: 2018-2023 Megan Conkle <megan.conkle@kdemail.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PREVIEWPROXY_H
#define PREVIEWPROXY_H

#include <QObject>
#include <QString>

namespace ghostwriterpp
{
/**
 * Web Channel Proxy to the HTML preview data. Object is shared between C++ and
 * JavaScript to pass the live preview's settings/data updates.
 */
class PreviewProxy : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructor.
     */
    explicit PreviewProxy(QObject *parent = nullptr);

    /**
     * Destructor.
     */
    virtual ~PreviewProxy();

    /**
     * Sets the HTML contents of the live preview browser.
     */
    void setHtmlContent(const QString &html);

    /**
     * Returns the HTML contents of the live preview browser.
     */
    Q_INVOKABLE QString htmlContent() const;
    Q_PROPERTY(QString htmlContent READ htmlContent NOTIFY htmlChanged)

    /**
     * Sets the CSS style sheet of the live preview browser.
     */
    void setStyleSheet(const QString &css);

    /**
     * Returns the CSS style sheet used in the live preview browser.
     */
    Q_INVOKABLE QString styleSheet() const;
    Q_PROPERTY(QString styleSheet READ styleSheet NOTIFY styleSheetChanged)

    /**
     * Sets whether math rendering is enabled in the live preview.
     */
    void setMathEnabled(bool enabled);

    /**
     * Returns true if math rendering is enabled in the live preview,
     * false otherwise.
     */
    Q_INVOKABLE bool mathEnabled() const;
    Q_PROPERTY(bool mathEnabled READ mathEnabled NOTIFY mathToggled)

    Q_INVOKABLE void beginPreviewEdit(const QString &kind, int start, int end);
    Q_INVOKABLE void applyPreviewEdit(const QString &text);
    Q_INVOKABLE void endPreviewEdit();
    Q_INVOKABLE void togglePreviewCheckbox(int offset, bool checked);

signals:
    /**
     * Emitted when the HTML content changes.
     */
    void htmlChanged(const QString &html);

    /**
     * Emitted when the style sheet changes.
     */
    void styleSheetChanged(const QString &css);

    /**
     * Emitted when the math rendering is toggled.
     */
    void mathToggled(bool enabled);

    /**
     * Emitted after a successful in-preview edit writes to the markdown source.
     * JS listeners use this to shift any data-gw-text-start / data-gw-text-end /
     * data-gw-checkbox-source attributes on sibling editables whose value is
     * >= fromPos by delta, so the next focusin on another editable reads
     * offsets that match the current document.
     */
    void previewEditOffsetsShifted(int fromPos, int delta);

private:
    QString m_htmlContent;
    QString m_styleSheet;
    bool m_mathEnabled;
};

} // namespace ghostwriterpp

#endif // PREVIEWPROXY_H
