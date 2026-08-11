/*
	Groovy MiSTer - switchres modeline resolution.

	Socket-free and switchres-header-free by design: switchres_wrapper.h pulls
	<windows.h>, so it is included only by groovy_switchres.cpp. Including it
	from a flycast-facing header risks the usual Windows header-ordering
	conflicts, and on MinGW those only surface on a Windows build.

	Turns (width, height, refresh, monitor preset) into a CRT modeline that has
	already been through the safety gate. Nothing here ever touches the host
	display - switchres is built SR_CALC_ONLY.
*/
#pragma once

#include "groovy_modeline.h"

#include <string>

namespace groovy
{

enum ModeResolveResult
{
	MODE_OK = 0,
	MODE_NOT_READY,     // switchres is not initialised
	MODE_BAD_PRESET,    // preset name is not one switchres knows
	MODE_REFUSED,       // switchres could not find a mode for this source
	MODE_SIZE_MISMATCH, // switchres returned a different active area than asked for
	MODE_GATED,         // the modeline failed the safety gate
};

struct ModeDecision
{
	ModeResolveResult result;
	GateResult gate;     // meaningful only when result == MODE_GATED
	Modeline modeline;   // meaningful only when result == MODE_OK
};

const char *modeResolveResultName(ModeResolveResult r);

// The 29 presets switchres actually recognises, extracted from
// monitor.cpp's monitor_set_preset(). Must stay in sync with it: switchres
// matches with a raw strcmp and CANNOT be asked whether a name was valid (it
// stores and echoes back whatever you set), so this list is the only guard.
bool isKnownMonitorPreset(const std::string& preset);
const char * const *monitorPresetList(int& count);

/*
	Wraps the switchres calculator.

	NOT a general-purpose class: switchres keeps its manager in a process-wide
	global (switchres_wrapper.cpp's `swr`), so at most one instance may be
	configured at a time. Constructing a second one while the first is live
	would have them fight over that global.
*/
class SwitchresCalc
{
public:
	SwitchresCalc() = default;
	~SwitchresCalc();

	SwitchresCalc(const SwitchresCalc&) = delete;
	SwitchresCalc& operator=(const SwitchresCalc&) = delete;

	// Validates the preset, brings switchres up and installs our log sink.
	// Safe to call repeatedly; re-configures if the preset or ini changed.
	bool configure(const std::string& preset, const std::string& iniPath);

	void shutdown();
	bool isReady() const { return inited; }

	const std::string& preset() const { return currentPreset; }

	// Resolve a source geometry to a gated modeline. Results are cached on an
	// integer key, so repeated calls at a steady video mode cost nothing and
	// never re-run switchres.
	ModeDecision resolve(int width, int height, double refreshHz,
			uint32_t bytesPerPixel, bool enforceCrtCap);

private:
	bool bringUp();

	bool inited = false;
	std::string currentPreset;
	std::string currentIni;

	// Cache key. Refresh is quantised to milli-Hz so it is an exact integer
	// compare - a double compare against a value derived from Frame_Cycles
	// would miss on the last bit and re-resolve every frame.
	struct CacheKey
	{
		int width = 0;
		int height = 0;
		int refreshMilliHz = 0;
		uint32_t bytesPerPixel = 0;
		bool enforceCrtCap = false;

		bool operator==(const CacheKey& o) const
		{
			return width == o.width && height == o.height
				&& refreshMilliHz == o.refreshMilliHz
				&& bytesPerPixel == o.bytesPerPixel
				&& enforceCrtCap == o.enforceCrtCap;
		}
	};

	CacheKey cachedKey;
	ModeDecision cachedDecision {};
	bool haveCached = false;
};

} // namespace groovy
