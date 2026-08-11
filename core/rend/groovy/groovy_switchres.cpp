/*
	Groovy MiSTer - switchres modeline resolution. See groovy_switchres.h.

	switchres has several API conventions that are easy to get backwards - the
	return-value polarities differ between calls, pclock is in Hz where the
	Groovy client wants MHz, and an unknown monitor preset degrades silently
	rather than failing. Each is explained at the call site it applies to.
	Read those before reordering anything here.
*/
#include "groovy_switchres.h"
#include "groovy_log.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// The only place switchres_wrapper.h (and therefore <windows.h>) is included.
#include "switchres_wrapper.h"

namespace groovy
{

// Extracted from core/deps/switchres/monitor.cpp, monitor_set_preset().
static const char * const kMonitorPresets[] =
{
	"pal", "ntsc", "generic_15",
	"arcade_15", "arcade_15ex", "arcade_25", "arcade_31",
	"arcade_15_25", "arcade_15_31", "arcade_15_25_31",
	"m2929", "d9800", "d9400", "d9200", "k7000", "k7131", "m3129",
	"h9110", "polo", "pstar", "ms2930", "ms929", "r666b",
	"pc_31_120", "pc_70_120",
	"vesa_480", "vesa_600", "vesa_768", "vesa_1024",
};
static const int kMonitorPresetCount = (int)(sizeof(kMonitorPresets) / sizeof(kMonitorPresets[0]));

const char * const *monitorPresetList(int& count)
{
	count = kMonitorPresetCount;
	return kMonitorPresets;
}

bool isKnownMonitorPreset(const std::string& preset)
{
	for (int i = 0; i < kMonitorPresetCount; i++)
		if (preset == kMonitorPresets[i])
			return true;
	return false;
}

const char *modeResolveResultName(ModeResolveResult r)
{
	switch (r)
	{
	case MODE_OK:            return "ok";
	case MODE_NOT_READY:     return "switchres not initialised";
	case MODE_BAD_PRESET:    return "unknown monitor preset";
	case MODE_REFUSED:       return "no mode fits this monitor";
	case MODE_SIZE_MISMATCH: return "switchres rescaled the mode";
	case MODE_GATED:         return "refused by safety gate";
	default:                 return "unknown";
	}
}

//
// switchres log sink.
//
// The switchres_manager constructor installs printf() as all three callbacks
// and sets verbosity 2 (switchres.cpp:119-122), and parsing a stray
// switchres.ini can re-install it (switchres.cpp:225). So these must be
// re-installed after EVERY sr_init(), not once at startup - otherwise switchres
// writes straight to stdout, which on a Windows GUI build goes nowhere and
// everywhere else floods the console with a block of text per resolve.
//
static void srLogError(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	// switchres terminates its lines; our logger adds its own framing.
	size_t len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	if (len > 0)
		logOnChange(LOGKEY_PRESET, "switchres: %s", buf);
}

static void srLogInfo(const char *fmt, ...)
{
	if (getLogLevel() < 1)
		return;
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	size_t len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	if (len > 0)
		logAlways("switchres: %s", buf);
}

static void srLogVerbose(const char *fmt, ...)
{
	if (getLogLevel() < 2)
		return;
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	size_t len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	if (len > 0)
		logAlways("switchres: %s", buf);
}

SwitchresCalc::~SwitchresCalc()
{
	shutdown();
}

bool SwitchresCalc::bringUp()
{
	// Order matters, and every step is a documented trap:
	//
	// 1. sr_init() constructs the manager (which installs printf as the log
	//    sink at verbosity 2) and parses any switchres.ini in the CWD.
	sr_init();

	// 2. Replace printf with our sink, then set the level. Callbacks first:
	//    set_log_* only installs into the live pointer if the current level
	//    permits, but always records into the _bak pointer that
	//    set_log_verbosity() re-derives from.
	sr_set_log_callback_error((void *)srLogError);
	sr_set_log_callback_info((void *)srLogInfo);
	sr_set_log_callback_debug((void *)srLogVerbose);
	// SR_ERROR=1, SR_INFO=2, SR_DEBUG=3. Our own callbacks gate finer detail on
	// getLogLevel(), so ask switchres for everything and filter there.
	sr_set_log_level(3);

	// 3. Optional user ini, AFTER sr_init's own parse so it wins.
	if (!currentIni.empty())
		sr_load_ini(const_cast<char *>(currentIni.c_str()));

	// 4. Monitor preset. Validated by the caller - switchres cannot be asked
	//    whether a name was valid, it stores and echoes back whatever we set.
	sr_set_monitor(currentPreset.c_str());

	// 5. Display. Returns an INDEX: 0 is the first display and means success,
	//    -1 is failure. `if (sr_init_disp(...))` would treat success as an error.
	//    "dummy" is the only backend a SR_CALC_ONLY build has.
	const int disp = sr_init_disp("dummy", nullptr);
	if (disp < 0)
	{
		notifyRefusal(LOGKEY_PRESET, "switchres display init failed");
		sr_deinit();
		return false;
	}

	inited = true;
	logAlways("switchres %s up, monitor preset '%s'%s%s", sr_get_version(),
			currentPreset.c_str(),
			currentIni.empty() ? "" : ", ini ",
			currentIni.empty() ? "" : currentIni.c_str());
	return true;
}

bool SwitchresCalc::configure(const std::string& preset, const std::string& iniPath)
{
	if (!isKnownMonitorPreset(preset))
	{
		// Do not pass it through. switchres would log one line nobody reads,
		// keep the previous (generic_15) ranges, and then echo the bogus name
		// back from sr_get_state() as if it were valid - a 15kHz-only monitor
		// that silently refuses every 31kHz mode.
		notifyRefusal(LOGKEY_PRESET, "unknown monitor preset '%s'", preset.c_str());
		shutdown();
		return false;
	}

	if (inited && preset == currentPreset && iniPath == currentIni)
		return true;

	shutdown();
	currentPreset = preset;
	currentIni = iniPath;
	return bringUp();
}

void SwitchresCalc::shutdown()
{
	if (!inited)
		return;
	// sr_deinit() is a bare `delete swr` - calling it without a live sr_init()
	// is a double free, hence the guard.
	sr_deinit();
	inited = false;
	haveCached = false;
}

ModeDecision SwitchresCalc::resolve(int width, int height, double refreshHz,
		uint32_t bytesPerPixel, bool enforceCrtCap)
{
	CacheKey key;
	key.width = width;
	key.height = height;
	key.refreshMilliHz = (int)std::lround(refreshHz * 1000.0);
	key.bytesPerPixel = bytesPerPixel;
	key.enforceCrtCap = enforceCrtCap;

	if (haveCached && key == cachedKey)
		return cachedDecision;

	ModeDecision decision {};
	decision.gate = GATE_OK;

	if (!inited)
	{
		decision.result = MODE_NOT_READY;
		return decision;
	}

	// A geometry change means a fresh switchres. get_mode() accumulates the
	// modes it creates, and a previously-added (now resolution-locked) mode can
	// win for a different source and come back rescaled. Measured against
	// flycast's own mode set this does not currently happen - every geometry we
	// emit has an exact match - but a re-init costs nothing at mode-change
	// frequency and turns that from an observation into a guarantee.
	// The real backstop is the size check below.
	if (haveCached && (cachedKey.width != width || cachedKey.height != height))
	{
		const std::string preset = currentPreset;
		const std::string ini = currentIni;
		shutdown();
		currentPreset = preset;
		currentIni = ini;
		if (!bringUp())
		{
			decision.result = MODE_NOT_READY;
			return decision;
		}
	}

	sr_mode srm;
	memset(&srm, 0, sizeof(srm));

	// Never ask for SR_MODE_INTERLACED. flycast hands us whole progressive
	// frames at the field rate, so let switchres weigh the progressive band
	// first; if it still lands on an interlaced modeline (480 lines on a
	// 15kHz-only monitor) we feed that as a PROGRESSIVE FRAMEBUFFER over an
	// interlaced signal - interlace byte 2 - and let the core derive the field
	// cadence. Byte 1 would require us to emit half-height fields, which we
	// never do.
	//
	// Returns 1 on success and 0 on FAILURE - the opposite polarity to
	// sr_init_disp above.
	if (sr_add_mode(width, height, refreshHz, 0, &srm) == 0)
	{
		decision.result = MODE_REFUSED;
		cachedKey = key;
		cachedDecision = decision;
		haveCached = true;
		return decision;
	}

	// We have no scaler: the blit is the renderer's offscreen target, verbatim.
	// If switchres answered with a different active area (a stretched or
	// accumulated mode) the picture would be wrong in a way no later check
	// catches, so refuse instead.
	if (srm.width != width || srm.height != height)
	{
		decision.result = MODE_SIZE_MISMATCH;
		cachedKey = key;
		cachedDecision = decision;
		haveCached = true;
		return decision;
	}

	Modeline m;
	// switchres reports the pixel clock in Hz; CmdSwitchres wants MHz. Getting
	// this wrong yields a plausible-looking modeline and no signal at all.
	m.pclock    = (double)srm.pclock / 1000000.0;
	m.hActive   = (uint16_t)srm.width;
	m.hBegin    = (uint16_t)srm.hbegin;
	m.hEnd      = (uint16_t)srm.hend;
	m.hTotal    = (uint16_t)srm.htotal;
	m.vActive   = (uint16_t)srm.height;
	m.vBegin    = (uint16_t)srm.vbegin;
	m.vEnd      = (uint16_t)srm.vend;
	m.vTotal    = (uint16_t)srm.vtotal;
	m.interlace = srm.interlace ? INTERLACE_PROGRESSIVE_FB : INTERLACE_PROGRESSIVE;

	const GateResult gate = gateModeline(m, bytesPerPixel, enforceCrtCap);
	if (gate != GATE_OK)
	{
		decision.result = MODE_GATED;
		decision.gate = gate;
	}
	else
	{
		decision.result = MODE_OK;
		decision.modeline = m;
	}

	cachedKey = key;
	cachedDecision = decision;
	haveCached = true;
	return decision;
}

} // namespace groovy
