/*
	Groovy MiSTer - frame capture from the renderer. See groovy_capture.h.
*/
#include "groovy_capture.h"
#include "groovy_log.h"
#include "groovy_output.h"

#include "types.h"
#include "hw/pvr/Renderer_if.h"
#include "cfg/option.h"
#include "stdclass.h"

#include <cstdio>

namespace groovy
{

static int dumpRemaining = 0;
static int dumpIndex = 0;
static std::string dumpDir;

FrameCapture& frameCapture()
{
	static FrameCapture instance;
	return instance;
}

bool FrameCapture::capture(int width, int height)
{
	if (renderer == nullptr || width <= 0 || height <= 0)
		return false;

	const size_t needed = (size_t)width * (size_t)height * 4;
	if (buffer.size() < needed)
		buffer.resize(needed);

	if (!renderer->ReadFrame(buffer.data(), width, height))
	{
		frameWidth = 0;
		frameHeight = 0;
		return false;
	}

	frameWidth = width;
	frameHeight = height;
	return true;
}

static void writeDump(const FrameCapture& fc)
{
	const int w = fc.width();
	const int h = fc.height();
	const uint8_t *src = fc.data();
	if (src == nullptr)
		return;

	char name[256];

	// .ppm - P6 is RGB, so this also proves the channel order: if the packer
	// contract is wrong the dump comes out with red and blue swapped, visibly.
	snprintf(name, sizeof(name), "%s/groovy_%03d.ppm", dumpDir.c_str(), dumpIndex);
	FILE *f = nowide::fopen(name, "wb");
	if (f != nullptr)
	{
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		std::vector<uint8_t> row((size_t)w * 3);
		for (int y = 0; y < h; y++)
		{
			const uint8_t *s = src + (size_t)y * w * 4;
			for (int x = 0; x < w; x++, s += 4)
			{
				row[x * 3 + 0] = s[2]; // R
				row[x * 3 + 1] = s[1]; // G
				row[x * 3 + 2] = s[0]; // B
			}
			fwrite(row.data(), 1, row.size(), f);
		}
		std::fclose(f);
	}

	// .raw - the exact BGRA bytes, for byte comparison against a known good.
	snprintf(name, sizeof(name), "%s/groovy_%03d.raw", dumpDir.c_str(), dumpIndex);
	f = nowide::fopen(name, "wb");
	if (f != nullptr)
	{
		fwrite(src, 1, (size_t)w * h * 4, f);
		std::fclose(f);
	}

	dumpIndex++;
}

void onFrameReady(int width, int height)
{
	if (!config::GroovyEnable)
		return;

	FrameCapture& fc = frameCapture();
	if (!fc.capture(width, height))
	{
		// Reported once per distinct message: a backend with no ReadFrame would
		// otherwise emit this 60 times a second.
		notifyRefusal(LOGKEY_READBACK,
				"cannot read frames from the current renderer (%dx%d)", width, height);
		return;
	}

	if (dumpRemaining > 0)
	{
		writeDump(fc);
		if (--dumpRemaining == 0)
			logAlways("frame dump complete: %d frame(s) in %s", dumpIndex, dumpDir.c_str());
	}

	// On to the wire. Rollback safety comes from where this whole path sits:
	// ggpo::advance_frame() calls rend_enable_renderer(false), QueueRender()
	// then recycles the TA context, and Renderer::Render() - our only caller -
	// is never reached. So a re-simulated frame cannot be observed here.
	submitFrame(fc.data(), fc.width(), fc.height());
}

void requestFrameDump(int count, const std::string& dir)
{
	dumpRemaining = count;
	dumpIndex = 0;
	dumpDir = dir.empty() ? get_writable_data_path("") : dir;
	logAlways("frame dump requested: %d frame(s) -> %s", count, dumpDir.c_str());
}

bool frameDumpPending()
{
	return dumpRemaining > 0;
}

void applyConfigOverrides()
{
	// Native resolution and geometry. The blit IS the renderer's offscreen
	// target, verbatim - there is no scaler anywhere in this path - so the
	// target has to be the Dreamcast's own 640x480 / 640x240 / 320x240.
	// Anything else and switchres gets asked for a modeline that either does
	// not exist or the safety gate refuses.
	config::RenderResolution.override(480);
	config::Widescreen.override(false);
	config::SuperWidescreen.override(false);
	config::Rotate90.override(false);
	config::ScreenStretching.override(100);

	// Skipped frames never reach Renderer::Render() (ta_ctx.cpp QueueRender
	// recycles the context), so they would silently drop out of the stream.
	config::SkipFrame.override(0);
	config::AutoSkipFrame.override(0);

	// Host vsync would block in the swap and fight the CRT's raster. Note the
	// user's own emu.cfg may well have this on even though the shipped default
	// is off, so this is not a no-op in practice.
	config::VSync.override(false);

	// The EmulateFramebuffer path renders into VRAM instead of leaving the
	// frame in the offscreen target ReadFrame() reads.
	config::EmulateFramebuffer.override(false);

	// Keeps the Fightcade detectors alive - see the header comment. Also
	// happens to be the threading model this whole module assumes: with it off,
	// Renderer::Render() and mainui_loop() are both plainly on the main thread,
	// so the Groovy client needs no locking.
	config::ThreadedRendering.override(false);

	setLogLevel(config::GroovyLogLevel);

	logAlways("config overrides applied: native 480, no widescreen/rotate/stretch, "
			"no frameskip, vsync off, framebuffer emulation off, single-threaded");
}

} // namespace groovy
