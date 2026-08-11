/*
	Groovy MiSTer - pixel packing to the wire format.

	Depends on nothing but <stdint.h> and <stddef.h> so it stays unit-testable
	outside the emulator. Do not add flycast headers here.

	Source format is the contract every Renderer::ReadFrame() implementation
	honours: tightly-packed BGRA8, top-down, stride = width * 4. That is what
	DX9 (D3DFMT_A8R8G8B8) and DX11 (DXGI_FORMAT_B8G8R8A8_UNORM) hold natively;
	the GL and Vulkan backends convert into it, because paying one swizzle in
	the backend is better than carrying a format enum through the whole path.

	Wire format is BGR-ordered (integration handoff 5.3):
	  RGB888 : [B][G][R] per pixel, tightly packed, stride = hActive*3
	  RGB565 : little-endian uint16, R in 15:11, G 10:5, B 4:0

	Getting this wrong shows up as a red/blue channel swap on the CRT and is the
	single most common first-integration bug - hence selfTest() below.
*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace groovy
{

// Bytes per pixel on the wire, matching the rgbMode passed to CmdInit.
enum RgbMode
{
	RGB_888  = 0, // 3 bytes, B G R
	RGB_A888 = 1, // 4 bytes, B G R A - A is carried but not displayed
	RGB_565  = 2, // 2 bytes, little-endian
};

// Bytes per pixel for a given wire mode, or 0 if unsupported.
uint32_t wireBytesPerPixel(RgbMode mode);

/*
	Pack `pixels` BGRA8 pixels from `src` into `dst` in the given wire mode.

	`dst` must hold pixels * wireBytesPerPixel(mode) bytes. Returns the number
	of bytes written, or 0 if the mode is unsupported - callers treat 0 as
	fatal for that frame rather than sending a truncated blit.
*/
size_t packFrame(uint8_t *dst, const uint8_t *src, size_t pixels, RgbMode mode);

// Individual packers, exposed for testing.
void packBGR888(uint8_t *dst, const uint8_t *src, size_t pixels);
void packBGRA888(uint8_t *dst, const uint8_t *src, size_t pixels);
void packRGB565(uint8_t *dst, const uint8_t *src, size_t pixels);

/*
	Known-pixel self-test. Returns 0 on success, or a non-zero code identifying
	the first failing case.

	Uses saturated pure red / green / blue specifically so a red/blue
	transposition cannot pass - a symmetric test pattern would.
*/
int selfTest();
const char *selfTestResultName(int code);

} // namespace groovy
