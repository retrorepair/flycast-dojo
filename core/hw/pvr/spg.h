#pragma once
#include "ta_ctx.h"

extern bool SH4FastEnough;

bool spg_Init();
void spg_Term();
void spg_Reset(bool Manual);
void spg_Serialize(Serializer& ser);
void spg_Deserialize(Deserializer& deser);

void CalculateSync();
void read_lightgun_position(int x, int y);
void scheduleRenderDone(TA_context *cntx);
void rescheduleSPG();

// Emulated video mode, for consumers that have to describe it to the outside
// world (Groovy MiSTer builds a CRT modeline from it).
enum class DCVideoStandard { NTSC, PAL, VGA };

struct DCVideoMode
{
	// Hz, at the cadence rend_vblank() actually fires. For an interlaced mode
	// that is the FIELD rate, because SPG halves the line time when
	// SPG_CONTROL.interlace is set (see CalculateSync), so one pass through
	// pvr_numscanlines is one field.
	double refreshRate;
	bool interlaced;
	DCVideoStandard standard;
};

// Derived from Frame_Cycles - i.e. from the same integer the scheduler paces
// vblanks with, not from the nominal 59.94/50 of the standard. A modeline built
// from anything else would drift against the emulator.
double spg_getRefreshRate();
DCVideoMode spg_getVideoMode();
