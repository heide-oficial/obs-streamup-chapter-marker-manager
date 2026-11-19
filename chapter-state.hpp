// Modified for heide-oficial's fork, 2026-11-19. GPL-2.0; see LICENSE.
#pragma once

#include <QMap>
#include <QRegularExpression>
#include <QString>

// Counters are keyed by the unnumbered type, never by a history row's display text.
class ChapterState {
public:
	QString preview(const QString &type) const { return type + " " + QString::number(counts.value(type) + 1); }
	void commit(const QString &type) { ++counts[type]; }
	void reset() { counts.clear(); }

private:
	QMap<QString, int> counts;
};

struct NativeChapter {
	QString output;
	QString name;
	QString timestamp;
};

// OBS 30.2+ logs actual muxer insertion, including generated native names.
// Read only these lines; plugin feedback and unrelated outputs are excluded.
inline bool parseNativeChapter(const QString &line, NativeChapter &chapter)
{
	static const QRegularExpression pattern(
		R"log(^\d{2}:\d{2}:\d{2}\.\d{3}: \[(?:mp4|mov) output: '(.+)'\] Adding chapter "(.*)" at (\d{2,}:\d{2}:\d{2})\.\d{3}\r?$)log");
	const auto match = pattern.match(line);
	if (!match.hasMatch())
		return false;
	chapter = {match.captured(1), match.captured(2), match.captured(3)};
	return true;
}
