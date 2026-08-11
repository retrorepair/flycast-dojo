/*
	Groovy MiSTer - session lifecycle and frame streaming. See groovy_output.h.
*/
#include "groovy_output.h"
#include "groovy_capture.h"
#include "groovy_input.h"
#include "groovy_log.h"
#include "groovy_pixels.h"
#include "groovy_switchres.h"

#include "types.h"
#include "cfg/option.h"
#include "hw/pvr/spg.h"
#include "network/ggpo.h"
#include "oslib/oslib.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

// The only translation unit that includes the client (and therefore winsock2).
#include "groovymister.h"

namespace groovy
{

static GroovyMister gm;
static SwitchresCalc switchres;

static bool sessionIsOpen = false;
static bool streaming = false;
static bool shutdownRequested = false;
static bool closeSent = false;
static bool logSinkInstalled = false;
// The inputs socket and its local port survive a video-side reconnect, so bind
// once per process rather than per session.
static bool inputsBound = false;

// Blit frame numbers must increase monotonically from 1. CmdInit re-zeroes the
// core's counter, so this resets with every session.
static uint32_t blitFrame = 0;
static uint32_t lastReconnectEpoch = 0;

// Connect back-off. A failed CmdInit costs up to 180ms inside the frame loop
// (three blocking getACK(60) calls), which under netplay is time GGPO is
// measuring - so offline we retry slowly and online we stand down entirely.
static const uint64_t CONNECT_RETRY_MS = 5000;
static uint64_t lastConnectFailMs = 0;
static bool connectGaveUp = false;

// Wire activity, for the keepalive. Stamped by anything we put on the socket.
static const uint64_t KEEPALIVE_IDLE_MS = 2000;
static uint64_t lastWireMs = 0;

// Dead-session detection. The client's own watchdog tests "frameEcho advanced",
// which garbage larger than the last value satisfies - so it never fires in the
// case it is most needed. Test plausibility instead.
static const uint32_t ECHO_SLACK = 8;
static const int MAX_BAD_ECHOS = 5;
static const int MAX_STALE_BLITS = 60;
static int badEchos = 0;
static int staleBlits = 0;
static uint32_t lastSeenEcho = 0;

// Beyond this the echo/frame spread is stale session state, not a resync.
// Matches the client's own RASTER_MAX_FRAME_SPREAD.
static const uint32_t RASTER_MAX_SPREAD = 8;

// Session parameters, captured at CmdInit so staleness can be judged against
// what we actually connected with rather than what the config now says.
//
// audioOn is in here because the rate and channel count ride CMD_INIT and
// cannot be changed mid-session - turning audio on or off needs a full
// reconnect, exactly like a codec or RGB-mode change.
struct SessionParams
{
	std::string host;
	int port = 0;
	int codec = 0;
	int rgbMode = 0;
	int mtu = 0;
	bool audioOn = false;
	// NLC pack and near level ride CMD_INIT byte[1] alongside the codec, so they
	// are equally un-changeable mid-session and equally a reason to reconnect.
	int nlcPack = 0;
	int nearLevel = 0;

	bool operator==(const SessionParams& o) const
	{
		return host == o.host && port == o.port && codec == o.codec
				&& rgbMode == o.rgbMode && mtu == o.mtu && audioOn == o.audioOn
				&& nlcPack == o.nlcPack && nearLevel == o.nearLevel;
	}
};
static SessionParams openParams;

// flycast's AICA output is signed 16-bit LE stereo at 44100Hz, which Groovy
// takes verbatim - no resampling, no channel juggling.
static const uint32_t AUDIO_RATE = 44100;
static const uint8_t AUDIO_CHANNELS = 2;
static const uint32_t AUDIO_BYTES_PER_FRAME = 4; // s16 x 2

// CMD_AUDIO's size field is a u16, so one datagram cannot carry more than
// 65535 bytes. At 44100Hz/60fps a video frame is ~735 sample frames (2940
// bytes), so this is generous headroom rather than a real constraint - but a
// stall that let the staging buffer grow unbounded would otherwise turn into a
// truncated or oversized send.
static const size_t AUDIO_MAX_BYTES = 16384 * AUDIO_BYTES_PER_FRAME;

// Staged samples waiting for the next CmdAudio. Guarded because WriteSample
// runs on whichever thread owns the SH4: applyConfigOverrides() pins
// ThreadedRendering off so that is normally the main thread, but the override
// only lands at Emulator::start(), so enabling Groovy mid-session would
// otherwise race. The lock is taken once per 512-frame chunk (~86/s), not per
// sample, so it costs nothing.
static std::mutex audioMutex;
static std::vector<uint8_t> audioStaging;

static Modeline currentModeline {};
static bool haveModeline = false;

static OutputStatus status;
static char stateText[128] = "off";

static double packBlitMs = 0.0;
static double syncMs = 0.0;
static double worstFrameMs = 0.0;
static uint64_t worstResetMs = 0;

static uint64_t nowMs()
{
	return (uint64_t)(os_GetSeconds() * 1000.0);
}

static void setState(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(stateText, sizeof(stateText), fmt, args);
	va_end(args);
	stateText[sizeof(stateText) - 1] = '\0';
}

static void clientLogSink(const char *msg)
{
	if (msg == nullptr)
		return;
	logOnChange(LOGKEY_SESSION, "client: %s", msg);
}

void init()
{
	if (!logSinkInstalled)
	{
		// Before anything else: on a Windows GUI build stdout goes nowhere, so
		// a failed connect would otherwise be completely invisible.
		gm_set_log_sink(clientLogSink);
		logSinkInstalled = true;
	}
	setLogLevel(config::GroovyLogLevel);
	gm.setVerbose((uint8_t)std::min(2, std::max(0, (int)config::GroovyLogLevel)));
}

static SessionParams currentConfigParams()
{
	SessionParams p;
	p.host = config::GroovyHost.get();
	p.port = config::GroovyPort;
	p.codec = config::GroovyCodec;
	p.rgbMode = config::GroovyRgbMode;
	p.mtu = config::GroovyMtu;
	p.audioOn = config::GroovyAudioMode != 0;
	p.nlcPack = config::GroovyNlcPack;
	p.nearLevel = config::GroovyNearLevel;
	return p;
}

bool submitAudio(const void *frames, int frameCount)
{
	if (!config::GroovyEnable || config::GroovyAudioMode == 0 || shutdownRequested)
		return false;
	if (frames == nullptr || frameCount <= 0)
		return false;
	// Nothing to send into, and no point staging samples that will be stale by
	// the time a session exists.
	if (!sessionIsOpen || !openParams.audioOn)
		return false;

	const size_t bytes = (size_t)frameCount * AUDIO_BYTES_PER_FRAME;
	{
		const std::lock_guard<std::mutex> lock(audioMutex);
		// Drop the oldest rather than growing without bound: if the video path
		// has stalled, fresher audio is worth more than a backlog, and CmdAudio
		// cannot send more than a u16 of bytes anyway.
		if (audioStaging.size() + bytes > AUDIO_MAX_BYTES)
		{
			const size_t excess = audioStaging.size() + bytes - AUDIO_MAX_BYTES;
			const size_t drop = std::min(excess, audioStaging.size());
			audioStaging.erase(audioStaging.begin(), audioStaging.begin() + drop);
		}
		const uint8_t *src = (const uint8_t *)frames;
		audioStaging.insert(audioStaging.end(), src, src + bytes);
	}

	// "MiSTer only" tells the caller to zero its own copy. It must zero rather
	// than skip the push: the host backend's play cursor has to keep moving for
	// its underrun logic, and doing it after we have taken our copy means a
	// dead MiSTer stream never leaves the user silent.
	//
	// Gated on fpga.audio as well as the setting, and that gate is load-bearing
	// now that MiSTer-only is the default: the core drops audio entirely when
	// its OSD Audio option is off, so silencing the host on the setting alone
	// would leave the user with no sound anywhere and nothing pointing at the
	// cause. Never silence the PC unless audio is genuinely going out.
	return config::GroovyAudioMode == 2 && gm.fpga.audio;
}

// Drain whatever WriteSample staged into one CmdAudio. Main thread only, from
// the frame path, so it is serialised with every other client call.
static void flushAudio()
{
	if (!sessionIsOpen || !openParams.audioOn)
		return;
	// Gate on what the CORE says: it drops audio when its OSD Audio option is
	// off, and this status bit is the only way to find out.
	if (!gm.fpga.audio)
	{
		logOnChange(LOGKEY_AUDIO, "MiSTer has audio disabled in its OSD - not sending");
		const std::lock_guard<std::mutex> lock(audioMutex);
		audioStaging.clear();
		return;
	}

	size_t bytes = 0;
	{
		const std::lock_guard<std::mutex> lock(audioMutex);
		if (audioStaging.empty())
			return;
		bytes = std::min(audioStaging.size(), (size_t)65535);
		// Whole sample frames only - half a frame would swap the channels for
		// everything after it.
		bytes -= bytes % AUDIO_BYTES_PER_FRAME;
		if (bytes == 0)
			return;

		char *audioBuffer = gm.getPBufferAudio();
		if (audioBuffer == nullptr)
			return;
		memcpy(audioBuffer, audioStaging.data(), bytes);
		audioStaging.erase(audioStaging.begin(), audioStaging.begin() + bytes);
	}

	gm.CmdAudio((uint16_t)bytes);
	lastWireMs = nowMs();
}

static void noteConnectFailed()
{
	lastConnectFailMs = nowMs();
	// One failed connect stands the whole session down under netplay: retrying
	// every 5s would put a 180ms stall into a match GGPO is timing.
	if (ggpo::active() || config::GGPOEnable)
	{
		connectGaveUp = true;
		logOnChange(LOGKEY_SESSION,
				"connect failed during netplay - standing down for this session");
	}
}

static bool connectRetryDue()
{
	if (connectGaveUp)
		return false;
	if (lastConnectFailMs == 0)
		return true;
	return nowMs() - lastConnectFailMs >= CONNECT_RETRY_MS;
}

static void resetSessionCounters()
{
	blitFrame = 0;
	badEchos = 0;
	staleBlits = 0;
	lastSeenEcho = 0;
	haveModeline = false;
	currentModeline = Modeline();
}

static void closeSession(const char *reason)
{
	if (!sessionIsOpen)
		return;

	// Exactly one CMD_CLOSE per session, aimed at the host/port we actually
	// connected to rather than whatever the config says now - editing the IP
	// mid-session must not misaddress the close.
	if (!closeSent)
	{
		gm.CmdSendClose();
		closeSent = true;
	}
	gm.CmdClose();

	sessionIsOpen = false;
	streaming = false;
	{
		// Stale samples must not survive into the next session: CmdInit fixes a
		// fresh rate/channel pairing, and the core restarts its own audio queue.
		const std::lock_guard<std::mutex> lock(audioMutex);
		audioStaging.clear();
	}
	resetSessionCounters();
	switchres.shutdown();
	setState("closed: %s", reason);
	logAlways("session closed (%s)", reason);
}

static bool openSession()
{
	const SessionParams params = currentConfigParams();

	if (!switchres.configure(config::GroovyMonitorPreset.get(), config::GroovySwitchresIni.get()))
	{
		// configure() has already surfaced the reason.
		noteConnectFailed();
		return false;
	}

	resetSessionCounters();
	closeSent = false;

	// Both of these MUST precede CmdInit. BindInputs opens the inputs socket and
	// sends the subscribe; setInputCaps rides CMD_INIT byte[5], and a 6-byte
	// CMD_INIT is silently discarded by cores older than version 2 - which is
	// why the client probes with CMD_GET_VERSION first and drops the caps byte
	// itself. Do not write an app-level v2->v1 fallback; that is the client's job.
	if (config::GroovyUseInputs)
	{
		if (!inputsBound)
		{
			gm.BindInputs(params.host.c_str(), (uint16_t)(int)config::GroovyInputPort);
			inputsBound = true;
		}
		else
		{
			// The inputs socket and its local port survive a video-side
			// reconnect, but the core only remembers the most recent
			// subscriber, so re-subscribe as UDP-loss insurance.
			gm.ResendInputSubscribe();
		}
		gm.setInputCaps(GM_CAP_INPUTS_V2 | GM_CAP_RUMBLE);
	}

	gm.setAutoReconnect(config::GroovyAutoReconnect ? 1 : 0);

	int codec = params.codec;
	// NLC + RGB565 is broken upstream (verified with no flycast code in the
	// path). Substitute rather than refuse - silently-but-loudly - so the user
	// still gets a picture.
	if (codec == 7 && params.rgbMode == RGB_565)
	{
		codec = 1;
		notifyRefusal(LOGKEY_CODEC, "NLC does not support RGB565 - using LZ4 for this session");
	}

	// NLC entropy pack and near level. These MUST be set before CmdInit - they
	// ride CMD_INIT byte[1] bits [7] and [3:2] alongside the codec itself.
	//
	// Without them the client keeps its own defaults of TILED and lossless
	// (groovymister.cpp:147-148), which is what the first hardware test hit:
	// byte[1] came out as 82 (NLC + TILED + lossless) instead of 214 (NLC +
	// Rice + near 1). Lossless peaks around 650KB a frame, which is over the
	// core's ingest ceiling, so frames arrived truncated - visible as
	// corruption along the bottom of the picture, with the core reporting
	// fskip on essentially every frame. The client's own comment on those
	// defaults says it: "rice+near1 clears the /59 ingest ceiling".
	//
	// Only meaningful for NLC, and deliberately not sent otherwise so the
	// LZ4 substitution path below cannot leave stale NLC bits in byte[1].
	if (codec == 7)
	{
		gm.setNlcPack((uint8_t)params.nlcPack);
		gm.setNearLevel((uint8_t)params.nearLevel);
	}

	// Rate and channels are fixed for the session by CMD_INIT, which is why
	// audioOn is part of SessionParams.
	const int rc = gm.CmdInit(params.host.c_str(), (uint16_t)params.port, codec,
			params.audioOn ? AUDIO_RATE : 0,
			params.audioOn ? AUDIO_CHANNELS : 0,
			(uint8_t)params.rgbMode, (uint16_t)params.mtu);
	if (rc != 0)
	{
		notifyRefusal(LOGKEY_SESSION, "cannot reach MiSTer at %s:%d", params.host.c_str(), params.port);
		noteConnectFailed();
		setState("no connection to %s", params.host.c_str());
		return false;
	}

	openParams = params;
	sessionIsOpen = true;
	lastConnectFailMs = 0;
	lastReconnectEpoch = gm.reconnectEpoch();
	lastWireMs = nowMs();
	resetLogKeys();
	setState("connected to %s:%d", params.host.c_str(), params.port);
	// Spell out the NLC parameters rather than just the codec number: the whole
	// of the first hardware test's corruption was these two silently sitting at
	// the client's defaults, and there was no host-side way to see it. Anyone
	// reading this line should be able to tell what CMD_INIT byte[1] will be
	// without decoding it by hand.
	if (codec == 7)
		logAlways("connected to %s:%d (NLC, pack %s, near %d, rgb %d, mtu %d, audio %s)",
				params.host.c_str(), params.port,
				params.nlcPack >= 2 ? "Rice" : "Tiled", params.nearLevel,
				params.rgbMode, params.mtu, params.audioOn ? "on" : "off");
	else
		logAlways("connected to %s:%d (codec %d, rgb %d, mtu %d, audio %s)",
				params.host.c_str(), params.port, codec, params.rgbMode, params.mtu,
				params.audioOn ? "on" : "off");
	return true;
}

// Resolve and, if it changed, send the modeline. Returns false if we have no
// usable mode for this source, in which case nothing is blitted this frame.
static bool ensureMode(int width, int height, uint32_t bytesPerPixel)
{
	const DCVideoMode video = spg_getVideoMode();
	if (video.refreshRate <= 0.0)
		return false;

	const ModeDecision decision = switchres.resolve(width, height, video.refreshRate,
			bytesPerPixel, config::GroovyCrtSafetyCap);

	if (decision.result != MODE_OK)
	{
		const char *detail = decision.result == MODE_GATED
				? gateResultName(decision.gate)
				: modeResolveResultName(decision.result);
		notifyRefusal(LOGKEY_MODELINE, "%dx%d @%.3fHz refused: %s",
				width, height, video.refreshRate, detail);
		streaming = false;
		setState("mode refused: %s", detail);
		return false;
	}

	if (haveModeline && currentModeline.sameSignal(decision.modeline))
		return true;

	const Modeline& m = decision.modeline;
	// CmdSwitchres returns int (0 = ACK'd). It used to be fire-and-forget UDP
	// and was lost every time on the reconnect path, which left the core
	// silently discarding every subsequent blit for the rest of the session.
	if (gm.CmdSwitchres(m.pclock, m.hActive, m.hBegin, m.hEnd, m.hTotal,
			m.vActive, m.vBegin, m.vEnd, m.vTotal, m.interlace) != 0)
	{
		notifyRefusal(LOGKEY_MODELINE, "modeline not acknowledged - reconnecting");
		closeSession("switchres ACK failed");
		noteConnectFailed();
		return false;
	}

	currentModeline = m;
	haveModeline = true;
	lastWireMs = nowMs();
	logAlways("mode %dx%d @%.3fHz -> %.4fMHz %d/%d/%d/%d %d/%d/%d/%d interlace=%u",
			width, height, video.refreshRate, m.pclock,
			m.hActive, m.hBegin, m.hEnd, m.hTotal,
			m.vActive, m.vBegin, m.vEnd, m.vTotal, m.interlace);
	setState("streaming %dx%d @%.2fHz", m.hActive, m.vActive, video.refreshRate);
	return true;
}

// The raster line each blit syncs to.
//
// vCountSync == 0 means "automatic frame delay", and that calculation is built
// entirely on m_emulationTime - which only WaitSync() writes, and which
// measures wall time BETWEEN WaitSync calls. Once something else owns the frame
// clock that interval becomes the whole frame period, the calculation
// degenerates to line 1 permanently, and "automatic" is a fiction. So under an
// external clock pick the line explicitly instead of pretending.
static uint16_t effectiveVCountSync()
{
	if (!pacingActive() && config::GroovyVCountSync == 0)
		return 1;
	return (uint16_t)(int)config::GroovyVCountSync;
}

// The client's watchdog cannot detect this: it tests whether frameEcho advanced,
// and garbage that happens to be larger reads as progress. Test plausibility.
static void checkSessionAlive()
{
	const uint32_t echo = gm.fpga.frameEcho;

	if (echo > blitFrame + ECHO_SLACK)
	{
		if (++badEchos >= MAX_BAD_ECHOS)
		{
			notifyRefusal(LOGKEY_SESSION,
					"session looks dead (echo %u vs blit %u) - reconnecting", echo, blitFrame);
			// Each failed CmdInit is up to 180ms of blocking getACK on this
			// thread, once a second. Do not let the client's own watchdog pile
			// that on top of our reconnect.
			gm.setAutoReconnect(0);
			closeSession("dead session");
			noteConnectFailed();
		}
		return;
	}
	badEchos = 0;

	if (echo == lastSeenEcho)
	{
		if (++staleBlits >= MAX_STALE_BLITS)
		{
			notifyRefusal(LOGKEY_SESSION, "no ACK movement in %d blits - reconnecting", MAX_STALE_BLITS);
			gm.setAutoReconnect(0);
			closeSession("no ACK movement");
			noteConnectFailed();
		}
	}
	else
	{
		staleBlits = 0;
		lastSeenEcho = echo;
	}
}

void submitFrame(const uint8_t *bgra, int width, int height)
{
	if (!config::GroovyEnable || shutdownRequested)
		return;
	if (bgra == nullptr || width <= 0 || height <= 0)
		return;

	const RgbMode rgbMode = (RgbMode)(int)config::GroovyRgbMode;
	const uint32_t bpp = wireBytesPerPixel(rgbMode);
	if (bpp == 0)
	{
		notifyRefusal(LOGKEY_CODEC, "unsupported RGB mode %d", (int)config::GroovyRgbMode);
		return;
	}

	// Reopen if the user changed something that rides CMD_INIT. Codec and RGB
	// mode cannot be changed mid-session; they are baked into CMD_INIT bytes.
	if (sessionIsOpen && !(openParams == currentConfigParams()))
	{
		closeSession("session parameters changed");
		lastConnectFailMs = 0;
		connectGaveUp = false;
	}

	if (!sessionIsOpen)
	{
		if (!connectRetryDue())
			return;
		if (!openSession())
			return;
	}

	// An auto-reconnect restarts the core's frame counter. Realign ours or the
	// raster servo has nothing usable to work from (it still runs, just paced
	// coarsely, because the client clamps the spread).
	const uint32_t epoch = gm.reconnectEpoch();
	if (epoch != lastReconnectEpoch)
	{
		lastReconnectEpoch = epoch;
		status.reconnects = epoch;
		blitFrame = 0;
		haveModeline = false;
		logAlways("client auto-reconnected (epoch %u) - realigning frame counter", epoch);
	}

	const uint64_t t0 = (uint64_t)(os_GetSeconds() * 1000000.0);

	if (!ensureMode(width, height, bpp))
		return;

	char *blitBuffer = gm.getPBufferBlit(0);
	if (blitBuffer == nullptr)
		return;

	// Never write pixels anywhere but this buffer: it is allocated and, on
	// Windows, registered with RIO at CmdInit. A memcpy in is fine; a pointer
	// swap is not.
	const size_t written = packFrame((uint8_t *)blitBuffer, bgra,
			(size_t)width * (size_t)height, rgbMode);
	if (written == 0)
		return;

	// Monotonically increasing from 1, resynced forward if the core has run
	// ahead of us.
	blitFrame++;
	if (gm.fpga.frame > blitFrame)
		blitFrame = gm.fpga.frame + 1;

	// field is always 0: we only ever emit interlace byte 0 or 2, and with 2
	// the core derives the field cadence from the modeline itself. Feeding an
	// alternating field index would make it read consecutive frames out of its
	// two field buffers and comb on horizontal motion.
	gm.CmdBlit(blitFrame, 0, effectiveVCountSync(), (uint32_t)(int)config::GroovyFdMarginNs, 0);

	// Either side of the blit is fine per the protocol; after keeps the video
	// datagram as early as possible, which is the whole point of the tap point.
	flushAudio();

	lastWireMs = nowMs();
	streaming = true;
	status.framesSent++;
	status.lastBlitBytes = (uint32_t)written;

	packBlitMs = ((uint64_t)(os_GetSeconds() * 1000000.0) - t0) / 1000.0;

	checkSessionAlive();
}

bool pacingActive()
{
	// Netplay excluded: flycast's FixedFrequency stays the clock there, which
	// is what both peers' matchmaking assumes.
	if (!config::GroovyEnable || !sessionIsOpen || !streaming)
		return false;
	return !ggpo::active() && !config::GGPOEnable;
}

bool sessionOpen()
{
	return sessionIsOpen;
}

void waitSync()
{
	if (!config::GroovyEnable || !sessionIsOpen)
		return;
	// Before the first CmdSwitchres the client has no modeline, so m_frameTime
	// and m_vTotal are 0 and the raster servo has nothing to work from.
	if (!streaming)
		return;

	// *** Nothing below may spin for seconds. ***
	//
	// WaitSync's sleep comes from DiffTimeRaster(), which is proportional to
	// (frameEcho - frame) - so a stale counter pair implies a sleep of half a
	// frame period PER frame of divergence, accumulating across iterations. A
	// spread of a few thousand is tens of seconds of busy-spin that never
	// returns: a force-kill, not a stutter.
	//
	// The vendored client now carries an equivalent clamp internally, so this
	// is belt-and-braces - but it is also the diagnostic. The client's own
	// report of this condition is a verbosity-2 per-frame log line, invisible
	// in exactly the logs a user would send us.
	const uint32_t echo = gm.fpga.frameEcho;
	const uint32_t coreFrame = gm.fpga.frame;
	if (echo > coreFrame && echo - coreFrame > RASTER_MAX_SPREAD)
	{
		logOnChange(LOGKEY_RASTERSPREAD,
				"raster desync: frameEcho %u vs core frame %u (spread %u) - skipping pacing",
				echo, coreFrame, echo - coreFrame);
		blitFrame = 0;
		syncMs = 0.0;
		return;
	}

	const uint64_t t0 = (uint64_t)(os_GetSeconds() * 1000000.0);
	gm.WaitSync();
	syncMs = ((uint64_t)(os_GetSeconds() * 1000000.0) - t0) / 1000.0;

	// Attribute the cost. When we own the clock this is deliberate sleep and
	// must not be charged against the frame budget; when something else does,
	// all of it is overhead.
	const double frameMs = packBlitMs + (pacingActive() ? 0.0 : syncMs);
	const uint64_t now = nowMs();
	if (now - worstResetMs > 5000)
	{
		worstFrameMs = frameMs;
		worstResetMs = now;
	}
	else if (frameMs > worstFrameMs)
	{
		worstFrameMs = frameMs;
	}
}

void keepAlive()
{
	if (!config::GroovyEnable || !sessionIsOpen || shutdownRequested)
		return;

	// Gated on wire silence rather than a free-running timer, so it is
	// structurally impossible for this to fire during normal play: the blit
	// path refreshes lastWireMs every ~16ms at 60fps. It exists for the times
	// flycast is alive but not blitting - the ImGui menu is open (mainui_loop
	// runs gui_display_ui() and no emulation), a game is loading, a mode was
	// refused - where the core would otherwise drop the session on its idle
	// timeout and free the CRT.
	const uint64_t now = nowMs();
	if (now - lastWireMs < KEEPALIVE_IDLE_MS)
		return;

	gm.CmdSendKeepAlive();
	lastWireMs = now;
}

//
// Inputs
//

static MisterPadSnapshot padState;

void pollInputs()
{
	if (!config::GroovyEnable || !config::GroovyUseInputs || !sessionIsOpen)
	{
		padState = MisterPadSnapshot();
		return;
	}

	gm.PollInputs();

	// Gate on the NEGOTIATED caps, never on what we requested. CmdInit probes
	// the core with CMD_GET_VERSION and silently connects as v1 against
	// anything older than version 2, in which case there are no analog triggers
	// and no rumble however we asked.
	const uint8_t caps = gm.getInputCaps();
	padState.available = true;
	padState.rumbleCap = (caps & GM_CAP_RUMBLE) != 0;

	padState.buttons[0] = gm.joyInputs.joy1;
	padState.buttons[1] = gm.joyInputs.joy2;

	padState.lx[0] = gm.joyInputs.joy1LXAnalog;
	padState.ly[0] = gm.joyInputs.joy1LYAnalog;
	padState.rx[0] = gm.joyInputs.joy1RXAnalog;
	padState.ry[0] = gm.joyInputs.joy1RYAnalog;
	padState.lx[1] = gm.joyInputs.joy2LXAnalog;
	padState.ly[1] = gm.joyInputs.joy2LYAnalog;
	padState.rx[1] = gm.joyInputs.joy2RXAnalog;
	padState.ry[1] = gm.joyInputs.joy2RYAnalog;

	// Triggers arrive only in v2 analog packets.
	if (caps & GM_CAP_INPUTS_V2)
	{
		padState.lt[0] = gm.joyInputs.joy1LTAnalog;
		padState.rt[0] = gm.joyInputs.joy1RTAnalog;
		padState.lt[1] = gm.joyInputs.joy2LTAnalog;
		padState.rt[1] = gm.joyInputs.joy2RTAnalog;
	}

	// Sticks are all zero unless the OSD has 'Joysticks = Analog'; treat a
	// non-zero stick as proof analog packets are flowing, so a digital-only
	// setup does not get its d-pad fought by a centred stick.
	padState.analog = padState.lx[0] || padState.ly[0] || padState.rx[0] || padState.ry[0]
			|| padState.lx[1] || padState.ly[1] || padState.rx[1] || padState.ry[1]
			|| padState.lt[0] || padState.rt[0] || padState.lt[1] || padState.rt[1];
}

const MisterPadSnapshot& padSnapshot()
{
	return padState;
}

void sendRumble(int player, uint8_t strong, uint8_t weak)
{
	// Diagnostic BEFORE the caps gate. A "no vibration" report then
	// distinguishes "we sent and the MiSTer dropped it" from "we never tried",
	// which matters because the MiSTer-side chain is negotiated cap ->
	// MiSTer.ini RUMBLE -> per-pad System > Controllers > <player> > Rumble.
	if (getLogLevel() >= 1)
		logAlways("rumble: player=%d strong=%u weak=%u cap=%d",
				player, strong, weak, padState.rumbleCap ? 1 : 0);

	if (!config::GroovyEnable || !config::GroovyUseInputs || !sessionIsOpen)
		return;
	if (!padState.rumbleCap)
		return;
	if (player < 0 || player > 1)
		return;

	gm.SendRumble((uint8_t)player, strong, weak);
}

void term()
{
	shutdownRequested = true;
	// Before the session goes: resetState() stops any running rumble, and the
	// pads must not outlive the client they read from.
	termInputDevices();
	closeSession("shutdown");
	switchres.shutdown();
	setState("off");
	// Last, so the close itself is recorded. The file is flushed per line, so
	// this is only tidiness - a crash that skips it still leaves a full log.
	closeLogFile();
}

void getStatus(OutputStatus& out)
{
	out = status;
	out.enabled = config::GroovyEnable;
	out.connected = sessionIsOpen;
	out.streaming = streaming;
	out.state = stateText;
	out.haveModeline = haveModeline;
	out.modeline = currentModeline;
	out.monitorPreset = switchres.preset();
	out.vramSynced = gm.fpga.vramSynced != 0;
	out.frameskip = gm.fpga.vgaFrameskip != 0;
	out.audioEnabled = gm.fpga.audio != 0;
	out.packBlitMs = packBlitMs;
	out.syncOverheadMs = pacingActive() ? 0.0 : syncMs;
	out.worstFrameMs = worstFrameMs;

	const DCVideoMode video = spg_getVideoMode();
	out.srcRefresh = video.refreshRate;
	out.srcInterlaced = video.interlaced;
	out.srcWidth = frameCapture().width();
	out.srcHeight = frameCapture().height();
}

} // namespace groovy
