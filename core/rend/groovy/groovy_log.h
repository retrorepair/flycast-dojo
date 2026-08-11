/*
	Groovy MiSTer - logging with on-change dedupe.

	Socket-free by design: this header is safe to include from anywhere in
	flycast. The vendored Groovy client pulls <winsock2.h>, so any header that
	reaches it drags Winsock into every translation unit that includes it -
	kept out of this one deliberately.

	The point of the dedupe is that most Groovy failure states are per-frame
	conditions. A refused modeline or a dead session would otherwise emit 60
	identical lines a second and bury the one line that explains it. Everything
	here logs on the RENDERER channel.
*/
#pragma once

#include <deque>
#include <string>

namespace groovy
{

// Keys for logOnChange/notifyRefusal. One per recurring condition: a message is
// only emitted when the formatted text for that key differs from the last one.
enum LogKey
{
	LOGKEY_SESSION,       // connect / disconnect / retry state
	LOGKEY_MODELINE,      // resolved or refused modeline
	LOGKEY_PRESET,        // monitor preset problems
	LOGKEY_GATE,          // modeline safety gate refusals
	LOGKEY_RASTERSPREAD,  // frameEcho vs frame divergence
	LOGKEY_CODEC,         // codec/RGB-mode substitutions
	LOGKEY_READBACK,      // per-backend frame capture problems
	LOGKEY_AUDIO,         // audio negotiation / gating
	LOGKEY_COUNT
};

// Always logged, no dedupe. For one-shot lifecycle events.
void logAlways(const char *fmt, ...);

// Logged only when the formatted text for `key` differs from the previous call
// with that key. Use for anything that can be evaluated every frame.
void logOnChange(LogKey key, const char *fmt, ...);

// logOnChange plus a user-visible OSD notification.
//
// A refusal the user is not told about gets reported to us as a hang - they see
// a black CRT and no explanation. Everything that stops the stream should come
// through here rather than logOnChange.
void notifyRefusal(LogKey key, const char *fmt, ...);

// Forget all dedupe state. Call on session open so a condition that recurs
// across sessions is reported again rather than swallowed.
void resetLogKeys();

// Recent lines, newest last, for the settings UI status panel. Bounded.
const std::deque<std::string>& logRing();

// Where the Groovy log file is written, whether or not it exists yet. Shown in
// the settings UI so the file can be found without hunting for it.
const std::string& logFileLocation();

// Close the log file. Called on shutdown; the file is flushed per line anyway,
// so this is tidiness rather than a correctness requirement.
void closeLogFile();

// 0 = errors + lifecycle, 1 = + telemetry, 2 = full trace. Mirrors the vendored
// client's setVerbose levels so one setting drives both.
void setLogLevel(int level);
int getLogLevel();

} // namespace groovy
