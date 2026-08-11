/*
	Groovy MiSTer - Vulkan frame readback. See vk_groovy_readback.h.
*/
#include "vk_groovy_readback.h"

#ifdef USE_GROOVY

#include "vulkan_context.h"
#include "utils.h"

#include <cstring>

void GroovyVkReadback::reset()
{
	if (mapped != nullptr && dstMemory)
	{
		VulkanContext::Instance()->GetDevice().unmapMemory(*dstMemory);
		mapped = nullptr;
	}
	commandBuffer.reset();
	commandPool.reset();
	fence.reset();
	dstImage.reset();
	dstMemory.reset();
	bufWidth = 0;
	bufHeight = 0;
	resourcesValid = false;
}

bool GroovyVkReadback::ensureResources(int width, int height)
{
	if (resourcesValid && bufWidth == width && bufHeight == height)
		return true;

	reset();

	VulkanContext *context = VulkanContext::Instance();
	if (context == nullptr)
		return false;
	vk::Device device = context->GetDevice();
	vk::PhysicalDevice physicalDevice = context->GetPhysicalDevice();
	if (!device || !physicalDevice)
		return false;

	// Prefer blitting into a linear BGRA image: vkCmdBlitImage does format
	// conversion, so the R/B swizzle happens on the GPU for free. Fall back to
	// copyImage (which demands identical formats, hence an RGBA destination and
	// a CPU swizzle) when the driver cannot blit into linear BGRA.
	vk::Format dstFormat = vk::Format::eB8G8R8A8Unorm;
	useBlit = true;
	{
		vk::FormatProperties srcProps;
		physicalDevice.getFormatProperties(vk::Format::eR8G8B8A8Unorm, &srcProps);
		if (!(srcProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc))
			useBlit = false;

		vk::FormatProperties dstProps;
		physicalDevice.getFormatProperties(dstFormat, &dstProps);
		if (!(dstProps.linearTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
			useBlit = false;
	}
	if (!useBlit)
		dstFormat = vk::Format::eR8G8B8A8Unorm;

	try {
		vk::ImageCreateInfo imageCreateInfo(vk::ImageCreateFlags(), vk::ImageType::e2D, dstFormat,
				vk::Extent3D(width, height, 1), 1, 1,
				vk::SampleCountFlagBits::e1, vk::ImageTiling::eLinear,
				vk::ImageUsageFlagBits::eTransferDst,
				vk::SharingMode::eExclusive, nullptr, vk::ImageLayout::eUndefined);
		dstImage = device.createImageUnique(imageCreateInfo);

		vk::MemoryRequirements memReq = device.getImageMemoryRequirements(*dstImage);
		const u32 memoryType = findMemoryType(physicalDevice.getMemoryProperties(), memReq.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
		dstMemory = device.allocateMemoryUnique(vk::MemoryAllocateInfo(memReq.size, memoryType));
		device.bindImageMemory(*dstImage, *dstMemory, 0);

		vk::ImageSubresource subresource(vk::ImageAspectFlagBits::eColor, 0, 0);
		vk::SubresourceLayout layout;
		device.getImageSubresourceLayout(*dstImage, &subresource, &layout);
		rowPitch = layout.rowPitch;
		memOffset = layout.offset;

		// Host-coherent, so map once and keep it mapped for the lifetime of the
		// resources rather than per frame.
		mapped = (u8 *)device.mapMemory(*dstMemory, 0, VK_WHOLE_SIZE);

		commandPool = device.createCommandPoolUnique(vk::CommandPoolCreateInfo(
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				context->GetGraphicsQueueFamilyIndex()));
		commandBuffer = std::move(device.allocateCommandBuffersUnique(
				vk::CommandBufferAllocateInfo(*commandPool, vk::CommandBufferLevel::ePrimary, 1)).front());
		fence = device.createFenceUnique(vk::FenceCreateInfo());
	} catch (const vk::SystemError& e) {
		WARN_LOG(RENDERER, "Groovy Vulkan readback setup failed: %s", e.what());
		reset();
		return false;
	}

	bufWidth = width;
	bufHeight = height;
	resourcesValid = true;
	return true;
}

bool GroovyVkReadback::read(vk::Image srcImage, int width, int height, u8 *dst)
{
	if (dst == nullptr || width <= 0 || height <= 0 || !srcImage)
		return false;
	if (!ensureResources(width, height))
		return false;

	VulkanContext *context = VulkanContext::Instance();
	vk::Device device = context->GetDevice();

	try {
		commandBuffer->reset(vk::CommandBufferResetFlags());
		commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

		// Destination -> transfer destination.
		vk::ImageMemoryBarrier barrier(vk::AccessFlags(), vk::AccessFlagBits::eTransferWrite,
				vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *dstImage, range);
		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags(), nullptr, nullptr, barrier);

		// Source -> transfer source. The render pass leaves the colour
		// attachment in eShaderReadOnlyOptimal (drawer.cpp's attachment
		// description, !EmulateFramebuffer branch), which is what we restore
		// it to below - PresentFrame samples it right after us.
		barrier = vk::ImageMemoryBarrier(vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, srcImage, range);
		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags(), nullptr, nullptr, barrier);

		const vk::ImageSubresourceLayers layers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		if (useBlit)
		{
			const vk::Offset3D blitSize(width, height, 1);
			const vk::ImageBlit imageBlit(layers, { vk::Offset3D(), blitSize },
					layers, { vk::Offset3D(), blitSize });
			commandBuffer->blitImage(srcImage, vk::ImageLayout::eTransferSrcOptimal,
					*dstImage, vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eNearest);
		}
		else
		{
			const vk::ImageCopy imageCopy(layers, vk::Offset3D(), layers, vk::Offset3D(),
					vk::Extent3D(width, height, 1));
			commandBuffer->copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal,
					*dstImage, vk::ImageLayout::eTransferDstOptimal, imageCopy);
		}

		// Destination -> general, required before mapping.
		barrier = vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *dstImage, range);
		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
				vk::DependencyFlags(), nullptr, nullptr, barrier);

		// Source back to where the render pass left it.
		barrier = vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead,
				vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, srcImage, range);
		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
				vk::DependencyFlags(), nullptr, nullptr, barrier);

		commandBuffer->end();

		device.resetFences(fence.get());
		std::vector<vk::CommandBuffer> buffers { *commandBuffer };
		context->SubmitCommandBuffers(buffers, *fence);

		// Synchronous by design, but bounded: a hung GPU must not wedge the
		// emulator forever. One second is far beyond any legitimate copy.
		if (device.waitForFences(fence.get(), true, 1000000000ull) != vk::Result::eSuccess)
			return false;
	} catch (const vk::SystemError& e) {
		WARN_LOG(RENDERER, "Groovy Vulkan readback failed: %s", e.what());
		return false;
	}

	if (mapped == nullptr)
		return false;

	const u8 *src = mapped + memOffset;
	const size_t rowBytes = (size_t)width * 4;
	for (int y = 0; y < height; y++)
	{
		const u8 *s = src + (size_t)y * rowPitch;
		u8 *out = dst + (size_t)y * rowBytes;
		if (useBlit)
		{
			// Already BGRA, and Vulkan images are top-down.
			memcpy(out, s, rowBytes);
		}
		else
		{
			for (int x = 0; x < width; x++, s += 4, out += 4)
			{
				out[0] = s[2]; // B <- R
				out[1] = s[1]; // G
				out[2] = s[0]; // R <- B
				out[3] = s[3]; // A
			}
		}
	}

	return true;
}

#endif // USE_GROOVY
