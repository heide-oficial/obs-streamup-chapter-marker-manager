// Modified for heide-oficial's fork, 2026-11-19. GPL-2.0; see LICENSE.
#pragma once

#include <QDir>
#include <QFileInfo>

inline QString chapterExportDirectory(const QString &videoPath)
{
	const QFileInfo video(videoPath);
	return QDir(video.absolutePath()).filePath(video.completeBaseName());
}
