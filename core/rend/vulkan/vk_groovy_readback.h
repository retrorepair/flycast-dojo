/*
	Groovy MiSTer - Vulkan frame readback.

	Shared by the plain and OIT Vulkan renderers: both hold their finished frame
	in an eR8G8B8A8Unorm colour attachment left in eShaderReadOnlyOptimal by the
	render pass, so the copy is identical - only the source image differs.

	Unlike VulkanContext::DoSwapAutomation() (the TEST_AUTOMATION screenshot
	path this is modelled on) everything here is cached across frames and the
	wait is on a private fence rather than graphicsQueue.waitIdle(). At 60Hz,
	allocating an image plus device memory per frame and stalling the whole
	queue would cost far more than the readback itself.
*/
#pragma once

#include "vulkan.h"

#include <vector>

#ifdef USE_GROOVY

class GroovyVkReadback
{
public:
	~GroovyVkReadback() { reset(); }

	// Copy `srcImage` (eR8G8B8A8Unorm, eShaderReadOnlyOptimal) into `dst` as
	// tightly-packed BGRA8, top-down. Returns false on any failure - a Groovy
	// frame that cannot be read must degrade to "no CRT output", never take
	// down the emulator mid-match.
	bool read(vk::Image srcImage, int width, int height, u8 *dst);

	void reset();

private:
	bool ensureResources(int width, int height);

	vk::UniqueImage dstImage;
	vk::UniqueDeviceMemory dstMemory;
	vk::UniqueCommandPool commandPool;
	vk::UniqueCommandBuffer commandBuffer;
	vk::UniqueFence fence;

	int bufWidth = 0;
	int bufHeight = 0;
	vk::DeviceSize rowPitch = 0;
	vk::DeviceSize memOffset = 0;
	u8 *mapped = nullptr;

	// True when the driver can blit into a linear eB8G8R8A8Unorm image, which
	// does the R/B swizzle on the GPU. Otherwise we copyImage (which requires
	// matching formats, so the destination stays RGBA) and swizzle on the CPU.
	bool useBlit = false;
	bool resourcesValid = false;
};

#endif // USE_GROOVY
