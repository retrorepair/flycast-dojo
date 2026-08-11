/*
	Groovy MiSTer - modeline type and safety gate.

	Deliberately depends on nothing but <stdint.h> so it can be unit-tested (and
	fuzzed) outside the emulator. Do not add flycast headers here.

	This file is the ONLY thing standing between switchres and the FPGA. The
	client derives the stream length from whatever modeline we hand it and
	clamps nothing in between, and switchres will happily compute a mode outside
	what the user's CRT can survive - so if a check is not here, it is nowhere.
*/
#pragma once

#include <stdint.h>

namespace groovy
{

// The interlace byte of CMD_SWITCHRES.
//
// We only ever emit 0 or 2. flycast hands us whole progressive frames at the
// FIELD rate (SPG halves the line time when interlaced, so rend_vblank fires
// once per field with a full-height framebuffer), which is exactly what mode 2
// describes. Emitting 1 would require us to actually split and send half-height
// fields, which we do not do.
enum : uint8_t
{
	INTERLACE_PROGRESSIVE    = 0, // progressive modeline, full frame per blit
	INTERLACE_INTERLACED_FB  = 1, // interlaced modeline, half-height field per blit
	INTERLACE_PROGRESSIVE_FB = 2, // interlaced modeline, full-height frame per blit
};

struct Modeline
{
	double   pclock;   // MHz. switchres reports Hz - convert at the boundary.
	uint16_t hActive, hBegin, hEnd, hTotal;
	uint16_t vActive, vBegin, vEnd, vTotal;
	uint8_t  interlace;

	// Does this describe the same signal as `o`? Used to decide whether a
	// CmdSwitchres is actually needed, so it compares the timings rather than
	// the struct - a re-resolved but identical mode must not cost a switch.
	bool sameSignal(const Modeline& o) const
	{
		return pclock == o.pclock
			&& hActive == o.hActive && hBegin == o.hBegin && hEnd == o.hEnd && hTotal == o.hTotal
			&& vActive == o.vActive && vBegin == o.vBegin && vEnd == o.vEnd && vTotal == o.vTotal
			&& interlace == o.interlace;
	}
};

// The client's blit buffer is a fixed allocation sized for 720x576x3 and, on
// Windows, registered with RIO at CmdInit. Overrunning it is not a graceful
// failure. Per FIELD when the framebuffer is interlaced - not our case, since
// we never emit INTERLACE_INTERLACED_FB.
static const uint32_t MAX_BLIT_BYTES = 1245312;

// A conservative envelope for a 15/31kHz arcade CRT. This is the only tier the
// user may switch off: it is a "protect my monitor" policy, not a correctness
// property. Dreamcast/NAOMI never exceed 640x480 anyway, so in practice this
// only ever fires if something upstream has gone wrong.
static const uint16_t CRT_SAFE_MAX_H = 1024;
static const uint16_t CRT_SAFE_MAX_V = 576;

// Sanity bounds on the pixel clock. flycast's real modes land between ~6.5MHz
// (320x240 @15kHz) and ~25.5MHz (640x480 @31kHz); this is deliberately much
// wider, because its job is to catch a garbage/uninitialised value, not to
// second-guess switchres.
static const double PCLOCK_MIN_MHZ = 1.0;
static const double PCLOCK_MAX_MHZ = 200.0;

enum GateResult
{
	GATE_OK = 0,
	GATE_MALFORMED,         // not a coherent modeline - never user-overridable
	GATE_OVER_BLIT_BUDGET,  // would overrun the client's buffer - never user-overridable
	GATE_OVER_CRT_ENVELOPE, // outside the CRT safety envelope - follows the user's setting
};

inline bool isWellFormed(const Modeline& m)
{
	if (m.pclock < PCLOCK_MIN_MHZ || m.pclock > PCLOCK_MAX_MHZ)
		return false;
	if (m.hActive == 0 || m.vActive == 0)
		return false;
	// Porches must be monotonically increasing and the active area must fit
	// inside the total. A modeline that violates this is not "ugly", it is a
	// PLL program the core cannot lock to.
	if (!(m.hActive <= m.hBegin && m.hBegin < m.hEnd && m.hEnd <= m.hTotal))
		return false;
	if (!(m.vActive <= m.vBegin && m.vBegin < m.vEnd && m.vEnd <= m.vTotal))
		return false;
	if (m.interlace > INTERLACE_PROGRESSIVE_FB)
		return false;
	return true;
}

inline bool fitsBlitBuffer(const Modeline& m, uint32_t bytesPerPixel)
{
	// 32-bit is ample (max is ~1.2MB) but do the multiply in 64-bit so a
	// nonsense modeline cannot wrap into a passing value.
	const uint64_t bytes = (uint64_t)m.hActive * (uint64_t)m.vActive * (uint64_t)bytesPerPixel;
	return bytes <= (uint64_t)MAX_BLIT_BYTES;
}

inline bool isWithinCrtSafeEnvelope(const Modeline& m)
{
	return m.hActive <= CRT_SAFE_MAX_H && m.vActive <= CRT_SAFE_MAX_V;
}

// Structural checks always run; only the CRT envelope follows a setting.
inline GateResult gateModeline(const Modeline& m, uint32_t bytesPerPixel, bool enforceCrtCap)
{
	if (!isWellFormed(m))
		return GATE_MALFORMED;
	if (!fitsBlitBuffer(m, bytesPerPixel))
		return GATE_OVER_BLIT_BUDGET;
	if (enforceCrtCap && !isWithinCrtSafeEnvelope(m))
		return GATE_OVER_CRT_ENVELOPE;
	return GATE_OK;
}

inline const char *gateResultName(GateResult r)
{
	switch (r)
	{
	case GATE_OK:                 return "ok";
	case GATE_MALFORMED:          return "malformed modeline";
	case GATE_OVER_BLIT_BUDGET:   return "exceeds blit buffer";
	case GATE_OVER_CRT_ENVELOPE:  return "outside CRT safety envelope";
	default:                      return "unknown";
	}
}

} // namespace groovy
