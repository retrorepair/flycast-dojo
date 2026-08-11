/*
	Groovy MiSTer - unit tests for the modeline gate and switchres resolution.

	Builds as a standalone binary against the vendored switchres only; the log
	sink is stubbed, because groovy_log.cpp is the one file in the module that
	pulls flycast headers. groovy_modeline.h and groovy_switchres.cpp are
	deliberately dependency-free so this can exist at all - keep them that way.

	Build/run:  cmake -DENABLE_CTEST=ON ... && ctest -R groovy
*/
#include "groovy_switchres.h"
#include "groovy_pixels.h"
#include "groovy_log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

// ---- stub log sink ---------------------------------------------------------
namespace groovy
{
static std::deque<std::string> g_ring;
static int g_level = 0;
static int g_notifyCount = 0;

static std::string fmtv(const char *f, va_list a)
{
	char b[512];
	vsnprintf(b, sizeof(b), f, a);
	return std::string(b);
}
void logAlways(const char *fmt, ...)
{
	va_list a; va_start(a, fmt); g_ring.push_back(fmtv(fmt, a)); va_end(a);
}
void logOnChange(LogKey, const char *fmt, ...)
{
	va_list a; va_start(a, fmt); g_ring.push_back(fmtv(fmt, a)); va_end(a);
}
void notifyRefusal(LogKey, const char *fmt, ...)
{
	va_list a; va_start(a, fmt); g_ring.push_back(fmtv(fmt, a)); va_end(a);
	g_notifyCount++;
}
void resetLogKeys() {}
const std::deque<std::string>& logRing() { return g_ring; }
void setLogLevel(int l) { g_level = l; }
int getLogLevel() { return g_level; }
}
// ---------------------------------------------------------------------------

using namespace groovy;

static int failures;

static void check(bool ok, const char *what)
{
	printf("  %-58s %s\n", what, ok ? "PASS" : "*** FAIL ***");
	if (!ok)
		failures++;
}

static void testPresetAllowlist()
{
	printf("=== preset allowlist ===\n");
	// switchres cannot be asked whether a preset name was valid: it stores and
	// echoes back whatever you set, keeps the previous ranges, and logs one
	// line. This list is therefore the only guard, and must stay in sync with
	// core/deps/switchres/monitor.cpp.
	int n = 0;
	monitorPresetList(n);
	check(n == 29, "preset list has 29 entries (matches monitor_set_preset)");
	check(isKnownMonitorPreset("arcade_15"), "arcade_15 known");
	check(isKnownMonitorPreset("arcade_15_25_31"), "arcade_15_25_31 known");
	check(isKnownMonitorPreset("ntsc") && isKnownMonitorPreset("pal"), "ntsc/pal known");
	check(!isKnownMonitorPreset("arcade15"), "typo rejected");
	check(!isKnownMonitorPreset("ARCADE_15"), "wrong case rejected");
	check(!isKnownMonitorPreset(""), "empty rejected");
}

static void testBadPresetRefuses()
{
	printf("\n=== an unknown preset must refuse, not silently degrade ===\n");
	SwitchresCalc sr;
	const int before = g_notifyCount;
	check(!sr.configure("totally_bogus", ""), "configure() rejects unknown preset");
	check(!sr.isReady(), "calculator left un-ready");
	check(g_notifyCount > before, "refusal surfaced to the user");
}

static void testRealVideoModes()
{
	printf("\n=== flycast's real video modes ===\n");
	// Every geometry getPvrFramebufferSize() can produce: width is the TA tile
	// clip (multiple of 32), height clamped to 240 or 480.
	struct Case { const char *label; const char *preset; int w, h; double hz; uint8_t interlace; };
	static const Case cases[] = {
		{ "NTSC 240p 640x240", "arcade_15",       640, 240, 59.945, INTERLACE_PROGRESSIVE    },
		{ "NTSC 480i 640x480", "arcade_15",       640, 480, 59.945, INTERLACE_PROGRESSIVE_FB },
		{ "VGA  mode 640x480", "arcade_15",       640, 480, 60.000, INTERLACE_PROGRESSIVE_FB },
		{ "PAL  240p 640x240", "arcade_15",       640, 240, 50.000, INTERLACE_PROGRESSIVE    },
		{ "PAL  480i 640x480", "arcade_15",       640, 480, 50.000, INTERLACE_PROGRESSIVE_FB },
		{ "NAOMI      320x240", "arcade_15",      320, 240, 59.945, INTERLACE_PROGRESSIVE    },
		{ "NAOMI      512x240", "arcade_15",      512, 240, 59.945, INTERLACE_PROGRESSIVE    },
		// On a 31kHz-capable monitor the same 480-line source becomes a real
		// progressive 480p mode, so the interlace byte must be 0, not 2.
		{ "31kHz 480p 640x480", "arcade_15_25_31", 640, 480, 59.945, INTERLACE_PROGRESSIVE   },
	};

	for (const Case& c : cases)
	{
		SwitchresCalc sr;
		if (!sr.configure(c.preset, ""))
		{
			printf("  %-40s *** configure(%s) failed ***\n", c.label, c.preset);
			failures++;
			continue;
		}
		const ModeDecision d = sr.resolve(c.w, c.h, c.hz, 3, true);
		if (d.result != MODE_OK)
		{
			printf("  %-40s *** %s ***\n", c.label, modeResolveResultName(d.result));
			failures++;
			continue;
		}
		const Modeline& m = d.modeline;
		bool ok = true;
		// Active area must match exactly - we have no scaler.
		if (m.hActive != c.w || m.vActive != c.h)
			ok = false;
		if (m.interlace != c.interlace)
			ok = false;
		// pclock must be MHz. A Hz value would be ~1.3e7 and produce a
		// plausible-looking modeline with no signal.
		if (!(m.pclock > 1.0 && m.pclock < 200.0))
			ok = false;
		printf("  %-40s %4dx%-4d %8.4fMHz il=%u  %s\n", c.label,
				m.hActive, m.vActive, m.pclock, m.interlace, ok ? "PASS" : "*** FAIL ***");
		if (!ok)
			failures++;
	}
}

static void testGate()
{
	printf("\n=== safety gate ===\n");
	const Modeline ok { 13.0, 640, 666, 727, 831, 240, 242, 245, 261, INTERLACE_PROGRESSIVE };
	check(gateModeline(ok, 3, true) == GATE_OK, "valid 240p modeline passes");

	// 720x576x3 is exactly the client's buffer size; x4 overruns it.
	const Modeline big { 13.0, 720, 740, 800, 864, 576, 580, 585, 625, INTERLACE_PROGRESSIVE };
	check(gateModeline(big, 3, false) == GATE_OK, "720x576x3 == budget exactly, passes");
	check(gateModeline(big, 4, false) == GATE_OVER_BLIT_BUDGET, "720x576x4 over budget");

	// Tier order: structural faults outrank budget faults.
	Modeline bigBad = big; bigBad.hBegin = 10;
	check(gateModeline(bigBad, 4, false) == GATE_MALFORMED, "malformed outranks over-budget");

	Modeline bad = ok; bad.hBegin = 10;
	check(gateModeline(bad, 3, true) == GATE_MALFORMED, "non-monotonic porches rejected");
	Modeline zero = ok; zero.vActive = 0;
	check(gateModeline(zero, 3, true) == GATE_MALFORMED, "zero vActive rejected");
	Modeline hzClock = ok; hzClock.pclock = 13000446.0;
	check(gateModeline(hzClock, 3, true) == GATE_MALFORMED, "Hz-instead-of-MHz pclock rejected");

	// The CRT envelope is the only user-disableable tier. To exercise it the
	// modeline has to be INSIDE the blit budget but outside the envelope,
	// otherwise the budget tier fires first and this proves nothing:
	// 1280x240x3 = 921,600 bytes (under budget) but 1280 > CRT_SAFE_MAX_H.
	Modeline wide { 26.0, 1280, 1300, 1350, 1400, 240, 244, 247, 261, INTERLACE_PROGRESSIVE };
	check(gateModeline(wide, 3, false) == GATE_OK, "1280x240 passes with CRT cap off");
	check(gateModeline(wide, 3, true) == GATE_OVER_CRT_ENVELOPE, "1280x240 refused with CRT cap on");
	// ...and the budget still outranks the envelope when both are violated.
	Modeline wideTall { 40.0, 1280, 1300, 1350, 1400, 600, 610, 615, 640, INTERLACE_PROGRESSIVE };
	check(gateModeline(wideTall, 3, true) == GATE_OVER_BLIT_BUDGET, "over-budget outranks CRT envelope");
}

static void testCacheAndReinit()
{
	printf("\n=== cache and geometry-change reinit ===\n");
	SwitchresCalc sr;
	check(sr.configure("arcade_15", ""), "configure(arcade_15)");

	const ModeDecision a = sr.resolve(640, 240, 59.945, 3, true);
	const ModeDecision b = sr.resolve(640, 240, 59.945, 3, true); // cached
	const ModeDecision c = sr.resolve(640, 480, 59.945, 3, true); // geometry change
	const ModeDecision d = sr.resolve(640, 240, 59.945, 3, true); // and back

	check(a.result == MODE_OK && b.result == MODE_OK
			&& c.result == MODE_OK && d.result == MODE_OK, "all four resolve");
	check(a.modeline.sameSignal(b.modeline), "repeated call is identical");
	// The one that matters: switchres accumulates modes, so returning to an
	// earlier geometry must not come back rescaled.
	check(a.modeline.sameSignal(d.modeline), "returning to a prior geometry is stable");
	check(!a.modeline.sameSignal(c.modeline), "a different geometry differs");
}

static void testPixels()
{
	printf("\n=== pixel packing ===\n");
	// The module's own known-pixel test: pure red/green/blue, chosen so a
	// red/blue transposition cannot pass. A symmetric pattern would.
	const int rc = selfTest();
	printf("  %-58s %s\n", "packer self-test", rc == 0 ? "PASS" : selfTestResultName(rc));
	if (rc != 0)
		failures++;

	check(wireBytesPerPixel(RGB_888) == 3, "RGB888 is 3 bytes/px");
	check(wireBytesPerPixel(RGB_A888) == 4, "RGBA888 is 4 bytes/px");
	check(wireBytesPerPixel(RGB_565) == 2, "RGB565 is 2 bytes/px");
	check(wireBytesPerPixel((RgbMode)99) == 0, "unknown mode is 0 bytes/px");

	// A full 640x480 frame must land exactly on the byte count the blit budget
	// and CmdBlit both assume.
	std::vector<uint8_t> src((size_t)640 * 480 * 4, 0x40);
	std::vector<uint8_t> dst((size_t)640 * 480 * 4);
	check(packFrame(dst.data(), src.data(), 640 * 480, RGB_888) == (size_t)640 * 480 * 3,
			"640x480 RGB888 packs to 921600 bytes");
	check(packFrame(dst.data(), src.data(), 640 * 480, RGB_565) == (size_t)640 * 480 * 2,
			"640x480 RGB565 packs to 614400 bytes");
}

int main()
{
	testPresetAllowlist();
	testBadPresetRefuses();
	testRealVideoModes();
	testGate();
	testCacheAndReinit();
	testPixels();

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
			failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
