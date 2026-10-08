/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SESSIONRESTORE_H
#define SESSIONRESTORE_H

#include <QSet>
#include <QSettings>

#include "bookmark.h"

namespace ghostwriterpp
{
inline BookmarkList loadPersistedTabs(QSettings &settings, QString &activePath)
{
    BookmarkList result;
    activePath.clear();
    const int savedActive = settings.value("Session/activeTab", -1).toInt();
    const int count = settings.beginReadArray("Session/openTabs");
    QSet<QString> seenPaths;

    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString path = settings.value("filePath").toString();
        if (path.isEmpty()) {
            continue;
        }

        Bookmark bookmark(path, settings.value("cursor", 0).toInt());
        if (i == savedActive) {
            activePath = bookmark.filePath();
        }
        if (!bookmark.isValid() || seenPaths.contains(bookmark.filePath())) {
            continue;
        }

        seenPaths.insert(bookmark.filePath());
        result.append(bookmark);
    }

    settings.endArray();
    return result;
}
} // namespace ghostwriterpp

#endif
