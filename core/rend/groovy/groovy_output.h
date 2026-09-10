/*
	Groovy MiSTer - session lifecycle and frame streaming.

	Socket-free header: groovymister.h pulls <winsock2.h>, so it is included
	only by groovy_output.cpp. flycast includes <windows.h> in several places,
	and <windows.h> ahead of <winsock2.h> drags in the older Winsock 1.1
	declarations, which then collide - so the client header must not escape
	into flycast-facing headers.

	THREADING: everything here runs on the main thread and nothing else.
	Renderer::Render() (which reaches submitFrame via groovy_capture) and
	mainui_loop() (which drives waitSync/keepAlive) are both main-thread in
	either of flycast's threading configurations, and applyConfigOverrides()
	pins ThreadedRendering off anyway. The vendored client is explicitly not
	thread-safe, so this is a property to preserve, not an accident.
*/
#pragma once

#include "groovy_modeline.h"

#include <cstdint>
#include <string>

namespace groovy
{

// Mirror of the live stream, for the settings UI and the OSD.
struct OutputStatus
{
	bool enabled = false;
	bool connected = false;
	bool streaming = false;
	std::string state = "off";

	int srcWidth = 0;
	int srcHeight = 0;
	double srcRefresh = 0.0;
	bool srcInterlaced = false;

	bool haveModeline = false;
	Modeline modeline {};
	std::string monitorPreset;

	uint32_t framesSent = 0;
	uint32_t framesRefused = 0;
	uint32_t lastBlitBytes = 0;
	uint32_t reconnects = 0;

	// Straight from the FPGA's ACK. vramSynced is not carried: it cannot detect the
	// underrun its name suggests (self-clears within a raster line, sampled once
	// per blit) and only ever means "the core is up". frameskip is the real signal.
	bool frameskip = false;
	bool audioEnabled = false;

	// Per-frame cost, milliseconds, rolling worst case over ~5s.
	double packBlitMs = 0.0;
	double syncOverheadMs = 0.0;
	double worstFrameMs = 0.0;
};

// Install the client's log sink. Call once, before anything else - on a Windows
// GUI build stdout goes nowhere, so without this a failed connect is invisible.
void init();

// Tear the session down. Idempotent, and safe with no session ever opened.
void term();

// Hand over a captured frame (BGRA8, top-down, stride = width*4). Opens the
// session lazily on the first frame and resolves the modeline as needed.
void submitFrame(const uint8_t *bgra, int width, int height);

/*
	Hand over one chunk of emulated audio: signed 16-bit LE, stereo interleaved,
	44100Hz - which is flycast's native AICA output, so nothing is converted.

	Called from the audio path (core/oslib/audiostream.cpp) once per 512-frame
	chunk, i.e. ~86 times a second, NOT per sample. It only stages the samples;
	they go out with the next frame's CmdAudio, on the main thread, because the
	vendored client is not thread-safe.

	Rollback-safe for free: sgc_if.cpp returns before WriteSample when
	settings.aica.muteAudio is set, and ggpo::advance_frame() sets exactly that
	around a re-simulation. So a rolled-back frame produces no samples at all.

	Returns true if the caller should silence its own copy (MiSTer-only mode).
*/
bool submitAudio(const void *frames, int frameCount);

/*
	Called from mainui_loop() once per iteration, unconditionally.

	Must run even on iterations where nothing was blitted - the ImGui menu is
	open, a game is loading, a mode was refused. On Windows WaitSync() is the
	only thing that drains the RIO send-completion queue, and letting that fill
	makes sends fail silently in a way that looks exactly like a dead core.

	When Groovy owns the clock this is also the pacing wait; when it does not,
	the frame period has already been spent by flycast's own FixedFrequency
	spin, so it falls straight through to the drain.
*/
void waitSync();

// Holds an idle session open against the core's idle timeout (5s default), which
// openSession() opts into. Gated on wire silence, so at 60fps it never sends.
void keepAlive();

// True when the CRT raster is the frame clock, which is offline only -
// under GGPO netplay flycast's FixedFrequency stays the clock.
bool pacingActive();

bool sessionOpen();

void getStatus(OutputStatus& out);

//
// MiSTer-side inputs
//
// The MiSTer's own pads are streamed to us on a second socket (32101), so a
// cabinet needs no host-side controller at all. This is a convenience, not a
// latency win - it adds a network hop compared with a stick plugged into the PC.
//

// Snapshot of the MiSTer's pads, in wire terms. Socket-free so the gamepad
// layer can consume it without pulling groovymister.h.
struct MisterPadSnapshot
{
	bool available = false;   // session live and inputs subscribed
	bool analog = false;      // OSD 'Joysticks = Analog'; sticks/triggers only flow then
	bool rumbleCap = false;   // GM_CAP_RUMBLE actually NEGOTIATED, not requested

	// Generic Button 1..12 plus d-pad, per player. Bit layout is the GM_JOY_*
	// one: 0-3 right/left/down/up, 4-15 buttons 1-12.
	uint32_t buttons[2] = { 0, 0 };

	int8_t lx[2] = { 0, 0 };
	int8_t ly[2] = { 0, 0 };
	int8_t rx[2] = { 0, 0 };
	int8_t ry[2] = { 0, 0 };
	uint8_t lt[2] = { 0, 0 };
	uint8_t rt[2] = { 0, 0 };
};

/*
	Drain the inputs socket. Main thread only.

	Call sites are the two places flycast samples LOCAL input - getLocalInput()
	and nextFrame()'s ggpo_add_local_input loop, both in core/network/ggpo.cpp.
	That placement is what makes MiSTer pads netplay-safe: nextFrame() returns at
	`if (inRollback) return true;` before ever reaching the sample, so a
	re-simulation cannot observe fresh input, and the pads are just another local
	device feeding the same GGPO input word.
*/
void pollInputs();

const MisterPadSnapshot& padSnapshot();

// Send on state change only: the core repeats the last value until replaced,
// and 0,0 stops. No-op unless rumble was negotiated.
void sendRumble(int player, uint8_t strong, uint8_t weak);

} // namespace groovy
