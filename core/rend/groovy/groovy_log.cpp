/*
	Groovy MiSTer - logging with on-change dedupe. See groovy_log.h.

	Two things here are the result of the first hardware test producing no logs
	at all, at any setting:

	1. LOG LEVELS. flycast's GENERIC_LOG is `if (v <= MAX_LOGLEVEL)`, and
	   MAX_LOGLEVEL is LWARNING (3) whenever NDEBUG is defined (core/log/Log.h).
	   LINFO is 4, so every INFO_LOG in a release build is removed by the
	   preprocessor - not filtered at runtime, deleted. This module used
	   INFO_LOG throughout and therefore emitted nothing from a RelWithDebInfo
	   binary. Everything here now uses NOTICE/WARN/ERROR, all of which survive.

	2. THE FILE SINK. flycast only writes a log file when [log] LogToFile is
	   set, which defaults to false, and a Windows GUI build has no console for
	   the fallback. So even the surviving lines went nowhere. We write our own
	   file, independent of flycast's LogManager, so a diagnostic artifact
	   exists by default on the build this feature ships in.
*/
#include "groovy_log.h"

#include "types.h"
#include "cfg/option.h"
#include "rend/gui.h"
#include "stdclass.h"

#include <cstdarg>
#include <cstdio>

namespace groovy
{

static const size_t MAX_RING = 96;

static std::string lastMessage[LOGKEY_COUNT];
static std::deque<std::string> ring;
static int logLevel = 0;

static FILE *logFile = nullptr;
static bool logFileTried = false;
static std::string logFilePath;

static void pushRing(const std::string& line)
{
	ring.push_back(line);
	while (ring.size() > MAX_RING)
		ring.pop_front();
}

/*
	Append one line to our own log file.

	Opened lazily on first write rather than at init, so a build with logging
	turned off never creates the file. Flushed per line on purpose: the failure
	modes worth diagnosing (a hang, a force-quit, a crash bypassing our
	shutdown path via breakpad) are exactly the ones where a buffered log is
	empty when you go to read it.
*/
static void writeToFile(const char *level, const std::string& msg)
{
	if (!config::GroovyLogToFile)
		return;

	if (!logFileTried)
	{
		logFileTried = true;
		logFilePath = get_writable_data_path("groovy.log");
		logFile = nowide::fopen(logFilePath.c_str(), "w");
		if (logFile != nullptr)
			fprintf(logFile, "--- flycast Groovy MiSTer log ---\n");
	}
	if (logFile == nullptr)
		return;

	fprintf(logFile, "[%s] %s\n", level, msg.c_str());
	fflush(logFile);
}

static std::string formatv(const char *fmt, va_list args)
{
	char buf[512];
	vsnprintf(buf, sizeof(buf), fmt, args);
	buf[sizeof(buf) - 1] = '\0';
	return std::string(buf);
}

void logAlways(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const std::string msg = formatv(fmt, args);
	va_end(args);

	// NOTICE, not INFO: LNOTICE is 1 and survives MAX_LOGLEVEL in release.
	NOTICE_LOG(RENDERER, "[groovy] %s", msg.c_str());
	writeToFile("info", msg);
	pushRing(msg);
}

void logOnChange(LogKey key, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const std::string msg = formatv(fmt, args);
	va_end(args);

	if (lastMessage[key] == msg)
		return;
	lastMessage[key] = msg;

	// WARN, not INFO: LWARNING is 3, the release ceiling. These are recurring
	// conditions that only print on change, so they are never spam.
	WARN_LOG(RENDERER, "[groovy] %s", msg.c_str());
	writeToFile("warn", msg);
	pushRing(msg);
}

void notifyRefusal(LogKey key, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const std::string msg = formatv(fmt, args);
	va_end(args);

	if (lastMessage[key] == msg)
		return;
	lastMessage[key] = msg;

	// Errors, not info: a refusal means the CRT is not getting a picture.
	ERROR_LOG(RENDERER, "[groovy] %s", msg.c_str());
	writeToFile("REFUSED", msg);
	pushRing(msg);

	const std::string osd = "MiSTer: " + msg;
	gui_display_notification(osd.c_str(), 5000);
}

void resetLogKeys()
{
	for (int i = 0; i < LOGKEY_COUNT; i++)
		lastMessage[i].clear();
}

const std::deque<std::string>& logRing()
{
	return ring;
}

const std::string& logFileLocation()
{
	// Resolved on demand so the settings UI can show where the file will go
	// before anything has been written to it.
	if (logFilePath.empty())
		logFilePath = get_writable_data_path("groovy.log");
	return logFilePath;
}

void closeLogFile()
{
	if (logFile != nullptr)
	{
		std::fclose(logFile);
		logFile = nullptr;
	}
	logFileTried = false;
}

void setLogLevel(int level)
{
	logLevel = level;
}

int getLogLevel()
{
	return logLevel;
}

} // namespace groovy
