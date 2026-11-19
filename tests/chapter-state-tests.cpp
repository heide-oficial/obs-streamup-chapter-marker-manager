// Fork behavior regression tests, 2026-11-19. GPL-2.0; see ../LICENSE.
#include "../chapter-state.hpp"
#include <QStringList>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	ChapterState state;
	const QStringList types{"Cut start", "Funny clip", "Funny clip", "Cut finish", "Cut start",
				"Mission start", "Cut finish", "Funny clip", "Mission end"};
	const QStringList expected{"Cut start 1", "Funny clip 1", "Funny clip 2", "Cut finish 1", "Cut start 2",
				   "Mission start 1", "Cut finish 2", "Funny clip 3", "Mission end 1"};
	for (qsizetype i = 0; i < types.size(); ++i) {
		require(state.preview(types[i]) == expected[i], "Independent type numbering failed");
		state.commit(types[i]);
	}
	require(state.preview("Funny clip") == "Funny clip 4", "Picker preview changed a counter");
	require(state.preview("Funny clip") == "Funny clip 4", "Cancelled picker consumed a number");
	state.reset();
	require(state.preview("Funny clip") == "Funny clip 1", "New recording did not reset counters");
	state.commit("Funny clip 2");
	require(state.preview("Funny clip 2") == "Funny clip 2 2", "A type with a trailing number was incorrectly stripped");

	NativeChapter chapter;
	require(parseNativeChapter("08:20:00.123: [mp4 output: 'adv_file_output'] Adding chapter \"Unnamed 3\" at 00:12:35.456", chapter), "Native MP4 marker not detected");
	require(chapter.name == "Unnamed 3" && chapter.timestamp == "00:12:35" && chapter.output == "adv_file_output", "Native marker fields differ from video log");
	require(parseNativeChapter("08:20:00.123: [mov output: 'simple_file_output'] Adding chapter \"Clip: \\\"funny\\\" — ação\" at 123:12:35.456\r", chapter), "MOV, Unicode, quotes, or long recordings not supported");
	require(!parseNativeChapter("08:20:00.123: [StreamUP Record Chapter Manager] Added chapter marker: Unnamed 3", chapter), "Plugin feedback imported as a native marker");
	require(!parseNativeChapter("08:20:00.123: [mp4 output: 'adv_file_output'] Adding chapter \"Unnamed 3\" at 00:12:", chapter), "Partial log line was imported");
	std::cout << "Chapter counters and native OBS log regression checks passed.\n";
}
