/*
	Groovy MiSTer - pixel packing to the wire format. See groovy_pixels.h.
*/
#include "groovy_pixels.h"

#include <cstring>

namespace groovy
{

uint32_t wireBytesPerPixel(RgbMode mode)
{
	switch (mode)
	{
	case RGB_888:  return 3;
	case RGB_A888: return 4;
	case RGB_565:  return 2;
	default:       return 0;
	}
}

void packBGR888(uint8_t *dst, const uint8_t *src, size_t pixels)
{
	// Source is already B,G,R,A - drop every fourth byte. No channel swap:
	// getting here with an RGBA source would be a backend bug, not something to
	// paper over in the packer.
	for (size_t i = 0; i < pixels; i++, src += 4, dst += 3)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
	}
}

void packBGRA888(uint8_t *dst, const uint8_t *src, size_t pixels)
{
	// Byte-identical to the source; the core carries alpha but does not display it.
	memcpy(dst, src, pixels * 4);
}

void packRGB565(uint8_t *dst, const uint8_t *src, size_t pixels)
{
	for (size_t i = 0; i < pixels; i++, src += 4, dst += 2)
	{
		const uint16_t b = src[0] >> 3;
		const uint16_t g = src[1] >> 2;
		const uint16_t r = src[2] >> 3;
		const uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
		// Explicit little-endian store rather than a uint16 write, so the
		// result does not depend on host endianness.
		dst[0] = (uint8_t)(v & 0xff);
		dst[1] = (uint8_t)(v >> 8);
	}
}

size_t packFrame(uint8_t *dst, const uint8_t *src, size_t pixels, RgbMode mode)
{
	switch (mode)
	{
	case RGB_888:
		packBGR888(dst, src, pixels);
		return pixels * 3;
	case RGB_A888:
		packBGRA888(dst, src, pixels);
		return pixels * 4;
	case RGB_565:
		packRGB565(dst, src, pixels);
		return pixels * 2;
	default:
		return 0;
	}
}

//
// Self-test
//

enum SelfTestResult
{
	ST_OK = 0,
	ST_BGR888_RED,
	ST_BGR888_GREEN,
	ST_BGR888_BLUE,
	ST_BGR888_SIZE,
	ST_BGRA888_COPY,
	ST_565_RED,
	ST_565_GREEN,
	ST_565_BLUE,
	ST_565_WHITE,
	ST_565_SIZE,
	ST_BAD_MODE,
};

const char *selfTestResultName(int code)
{
	switch (code)
	{
	case ST_OK:            return "ok";
	case ST_BGR888_RED:    return "BGR888 red channel (R/B swapped?)";
	case ST_BGR888_GREEN:  return "BGR888 green channel";
	case ST_BGR888_BLUE:   return "BGR888 blue channel (R/B swapped?)";
	case ST_BGR888_SIZE:   return "BGR888 byte count";
	case ST_BGRA888_COPY:  return "BGRA888 passthrough";
	case ST_565_RED:       return "RGB565 red channel (R/B swapped?)";
	case ST_565_GREEN:     return "RGB565 green channel";
	case ST_565_BLUE:      return "RGB565 blue channel (R/B swapped?)";
	case ST_565_WHITE:     return "RGB565 saturated white";
	case ST_565_SIZE:      return "RGB565 byte count";
	case ST_BAD_MODE:      return "unsupported mode not refused";
	default:               return "unknown";
	}
}

int selfTest()
{
	// Four known pixels in the source contract's byte order: B, G, R, A.
	static const uint8_t src[4 * 4] = {
		0x00, 0x00, 0xff, 0xff, // pure red
		0x00, 0xff, 0x00, 0xff, // pure green
		0xff, 0x00, 0x00, 0xff, // pure blue
		0xff, 0xff, 0xff, 0xff, // white
	};
	uint8_t dst[4 * 4];

	// --- BGR888 -----------------------------------------------------------
	memset(dst, 0xAA, sizeof(dst));
	size_t n = packFrame(dst, src, 4, RGB_888);
	if (n != 12)
		return ST_BGR888_SIZE;
	// Red pixel must land as B=00 G=00 R=ff. If R and B were swapped this
	// would be ff,00,00 - which is exactly the bug this catches.
	if (!(dst[0] == 0x00 && dst[1] == 0x00 && dst[2] == 0xff))
		return ST_BGR888_RED;
	if (!(dst[3] == 0x00 && dst[4] == 0xff && dst[5] == 0x00))
		return ST_BGR888_GREEN;
	if (!(dst[6] == 0xff && dst[7] == 0x00 && dst[8] == 0x00))
		return ST_BGR888_BLUE;

	// --- BGRA888 ----------------------------------------------------------
	memset(dst, 0xAA, sizeof(dst));
	n = packFrame(dst, src, 4, RGB_A888);
	if (n != 16 || memcmp(dst, src, 16) != 0)
		return ST_BGRA888_COPY;

	// --- RGB565 -----------------------------------------------------------
	memset(dst, 0xAA, sizeof(dst));
	n = packFrame(dst, src, 4, RGB_565);
	if (n != 8)
		return ST_565_SIZE;
	// red -> 0xF800, little-endian on the wire = 00 F8
	if (!(dst[0] == 0x00 && dst[1] == 0xf8))
		return ST_565_RED;
	// green -> 0x07E0 = E0 07
	if (!(dst[2] == 0xe0 && dst[3] == 0x07))
		return ST_565_GREEN;
	// blue -> 0x001F = 1F 00
	if (!(dst[4] == 0x1f && dst[5] == 0x00))
		return ST_565_BLUE;
	// white -> 0xFFFF
	if (!(dst[6] == 0xff && dst[7] == 0xff))
		return ST_565_WHITE;

	// --- refusal ----------------------------------------------------------
	if (packFrame(dst, src, 4, (RgbMode)99) != 0)
		return ST_BAD_MODE;

	return ST_OK;
}

} // namespace groovy
