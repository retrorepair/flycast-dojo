/*
	Groovy MiSTer - frame capture from the renderer.

	Socket-free: this is the backend-facing half of the module. The
	network-facing half (groovy_output) sits behind it.

	Each renderer backend calls onFrameReady() from inside Render(), at the
	point its offscreen target holds the finished emulated frame and before
	DrawOSD()/displayFramebuffer(). See Renderer::ReadFrame().
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace groovy
{

/*
	The captured frame, in the ReadFrame contract's format: tightly-packed
	BGRA8, top-down, stride = width * 4.

	Main thread only. Renderer::Render() is executed by the main thread in both
	of flycast's threading configurations - with ThreadedRendering off the SH4
	runs on the main thread and PvrMessageQueue::enqueue() executes inline; with
	it on, the main thread drains the queue. So no locking is needed here, and
	adding some would only hide a future mistake.
*/
class FrameCapture
{
public:
	// Pull the finished frame out of the active renderer. Returns false if the
	// backend has no ReadFrame implementation or the read failed.
	bool capture(int width, int height);

	const uint8_t *data() const { return buffer.empty() ? nullptr : buffer.data(); }
	int width() const { return frameWidth; }
	int height() const { return frameHeight; }
	size_t pixels() const { return (size_t)frameWidth * (size_t)frameHeight; }
	bool valid() const { return frameWidth > 0 && frameHeight > 0 && !buffer.empty(); }

private:
	std::vector<uint8_t> buffer;
	int frameWidth = 0;
	int frameHeight = 0;
};

FrameCapture& frameCapture();

// Called by every renderer backend from inside Render(). No-op unless Groovy
// output is enabled.
void onFrameReady(int width, int height);

/*
	Dump the next `count` captured frames to `dir` as .ppm (viewable) plus .raw
	(the exact BGRA bytes).

	This is the bring-up check for a new backend: it answers "is the image the
	right way up" and "are R and B the right way round" in one look, without a
	MiSTer attached. A red/blue swap or a vertical flip is the most common
	first-integration bug and is invisible in a byte count.
*/
void requestFrameDump(int count, const std::string& dir);
bool frameDumpPending();

/*
	Force the settings the CRT path requires, using Option::override() so the
	settings widgets grey out and the user can see why.

	Called from Emulator::start(), alongside the existing GGPO override that set
	the precedent there.

	ThreadedRendering is in this list for a reason that has nothing to do with
	rendering: dojo.UpdateScore() - the Fightcade round/match detector - is only
	reached from Emulator::run(), which the threaded emulation loop never calls.
	Shipped Fightcade config has it off, so ranked works today by configuration
	rather than by construction; pinning it here makes that structural for as
	long as Groovy is on. See core/dojo/DojoSession.cpp UpdateScore().
*/
void applyConfigOverrides();

} // namespace groovy
