/*
	Groovy MiSTer - MiSTer-side pads as flycast gamepads. See groovy_input.h.
*/
#include "groovy_input.h"
#include "groovy_output.h"
#include "groovy_log.h"

#include "types.h"
#include "cfg/option.h"
#include "input/gamepad_device.h"

#include <array>
#include <memory>

namespace groovy
{

// Wire button positions. Mirrors the GM_JOY_* bit layout, but declared here so
// this file does not need groovymister.h (and therefore <winsock2.h>).
enum MisterButton
{
	MB_RIGHT = 0, MB_LEFT, MB_DOWN, MB_UP,
	MB_B1, MB_B2, MB_B3, MB_B4,
	MB_B5, MB_B6, MB_B7, MB_B8,
	MB_B9, MB_B10, MB_B11, MB_B12,
	MB_COUNT
};

// Analog axis codes, local to this device.
enum MisterAxis { MA_LX = 0, MA_LY, MA_RX, MA_RY, MA_LT, MA_RT, MA_COUNT };

/*
	Default mapping.

	Positions, not names: the MiSTer sends generic Button 1..12 and each
	physical controller maps itself on the MiSTer side via its own .map file, so
	this is written once against the wire layout rather than per controller.

	Two variants, following flycast's own conventions (see the arcade/console
	branches of core/sdl/sdl_gamepad.h's DefaultInputMapping) rather than
	inventing a layout:

	  ARCADE (NAOMI - CVS2, MVC2, KOF XI...)
	    Buttons 1-6 are the six action buttons in two rows of three, which
	    flycast expects as A B C / X Y Z. Buttons 7 and 8 are NAOMI's buttons 7
	    and 8, which flycast carries on DC_DPAD2_LEFT/RIGHT, and service is
	    DC_DPAD2_UP.

	  CONSOLE (Dreamcast)
	    Only four face buttons exist, so 1-4 are A B X Y. Buttons 5/6 become
	    Z and C rather than the analog triggers, because the MiSTer sends real
	    analog LT/RT on their own axes - the same choice flycast makes when a
	    pad has genuine trigger axes.

	Wire position 8 is Start on every controller the MiSTer normalises, so it is
	Start in both. 11 and 12 are stick clicks and are deliberately left free.
*/
class MisterInputMapping : public InputMapping
{
public:
	explicit MisterInputMapping(bool arcade)
	{
		name = arcade ? "MiSTer Arcade" : "MiSTer Default";

		set_button(DC_DPAD_RIGHT, MB_RIGHT);
		set_button(DC_DPAD_LEFT,  MB_LEFT);
		set_button(DC_DPAD_DOWN,  MB_DOWN);
		set_button(DC_DPAD_UP,    MB_UP);
		set_button(DC_BTN_START,  MB_B8);

		if (arcade)
		{
			// 1  2  3  4  5  6
			// A  B  C  X  Y  Z
			set_button(DC_BTN_A, MB_B1);
			set_button(DC_BTN_B, MB_B2);
			set_button(DC_BTN_C, MB_B3);
			set_button(DC_BTN_X, MB_B4);
			set_button(DC_BTN_Y, MB_B5);
			set_button(DC_BTN_Z, MB_B6);
			set_button(DC_DPAD2_LEFT,  MB_B7);   // NAOMI button 7
			set_button(DC_DPAD2_RIGHT, MB_B9);   // NAOMI button 8
			set_button(DC_DPAD2_UP,    MB_B10);  // service
		}
		else
		{
			set_button(DC_BTN_A, MB_B1);
			set_button(DC_BTN_B, MB_B2);
			set_button(DC_BTN_X, MB_B3);
			set_button(DC_BTN_Y, MB_B4);
			set_button(DC_BTN_Z, MB_B5);
			set_button(DC_BTN_C, MB_B6);
		}

		set_axis(0, DC_AXIS_LEFT,  MA_LX, false);
		set_axis(0, DC_AXIS_RIGHT, MA_LX, true);
		set_axis(0, DC_AXIS_UP,    MA_LY, false);
		set_axis(0, DC_AXIS_DOWN,  MA_LY, true);
		set_axis(0, DC_AXIS2_LEFT,  MA_RX, false);
		set_axis(0, DC_AXIS2_RIGHT, MA_RX, true);
		set_axis(0, DC_AXIS2_UP,    MA_RY, false);
		set_axis(0, DC_AXIS2_DOWN,  MA_RY, true);
		// Real analog triggers, when the MiSTer OSD has Joysticks = Analog.
		set_axis(0, DC_AXIS_LT, MA_LT, true);
		set_axis(0, DC_AXIS_RT, MA_RT, true);

		dirty = false;
	}
};

class MisterGamepad : public GamepadDevice
{
public:
	MisterGamepad(int player)
		: GamepadDevice(player, "MiSTer"), player(player)
	{
		_name = "MiSTer Pad " + std::to_string(player + 1);
		_unique_id = "mister_pad_" + std::to_string(player + 1);
		if (!find_mapping())
			input_mapper = getDefaultMapping();
		// Starts false and is re-evaluated in apply(): the device now exists
		// before any session does, so at construction there is nothing to have
		// negotiated with yet.
		rumbleEnabled = false;
	}

	// Honour the Controls tab's "reset to default" and its arcade/console
	// toggle, the same way SDLGamepad does. The `gamepad` flag distinguishes a
	// pad from a hitbox on SDL; it means nothing here, because the MiSTer has
	// already normalised whatever is plugged in to generic button positions.
	void resetMappingToDefault(bool arcade, bool gamepad) override
	{
		input_mapper = std::make_shared<MisterInputMapping>(arcade);
	}

	const char *get_button_name(u32 code) override
	{
		static const char *names[MB_COUNT] = {
			"Right", "Left", "Down", "Up",
			"Button 1", "Button 2", "Button 3", "Button 4",
			"Button 5", "Button 6", "Button 7", "Button 8",
			"Button 9", "Button 10", "Button 11", "Button 12",
		};
		return code < MB_COUNT ? names[code] : nullptr;
	}

	const char *get_axis_name(u32 code) override
	{
		static const char *names[MA_COUNT] = {
			"Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y",
			"Left Trigger", "Right Trigger",
		};
		return code < MA_COUNT ? names[code] : nullptr;
	}

	void rumble(float power, float inclination, u32 duration_ms) override
	{
		if (!config::GroovyRumble)
			return;
		const uint8_t strong = (uint8_t)(std::min(1.f, std::max(0.f, power)) * 255.f);
		// The MiSTer takes a strong/weak pair; flycast gives power plus an
		// inclination that biases towards the high-frequency motor.
		const uint8_t weak = (uint8_t)(std::min(1.f, std::max(0.f, power * inclination)) * 255.f);
		setRumble(strong, weak);
	}

	void update_rumble() override
	{
		// Nothing time-based to maintain: the core repeats the last value until
		// it is replaced, so there is no effect to re-arm each frame. Stopping
		// is an explicit 0,0 from rumble().
	}

	// Feed one frame's worth of wire state through the mapping.
	void apply(const MisterPadSnapshot& snap)
	{
		// Track the NEGOTIATED capability, not what we requested - caps are only
		// known once CmdInit has probed the core, which is after construction.
		rumbleEnabled = snap.rumbleCap && config::GroovyRumble;

		const uint32_t buttons = snap.buttons[player];
		for (u32 i = 0; i < MB_COUNT; i++)
		{
			const bool pressed = (buttons & (1u << i)) != 0;
			if (pressed != lastButtons[i])
			{
				lastButtons[i] = pressed;
				gamepad_btn_input(i, pressed);
			}
		}

		// Sticks and triggers only flow when the OSD has Joysticks = Analog.
		// Feeding a permanently-centred stick would fight the d-pad, so skip it
		// entirely when no analog packet has been seen.
		if (!snap.analog)
			return;

		applyAxis(MA_LX, snap.lx[player] * 256);
		applyAxis(MA_LY, snap.ly[player] * 256);
		applyAxis(MA_RX, snap.rx[player] * 256);
		applyAxis(MA_RY, snap.ry[player] * 256);
		applyAxis(MA_LT, snap.lt[player] * 128);
		applyAxis(MA_RT, snap.rt[player] * 128);
	}

	void resetState()
	{
		for (u32 i = 0; i < MB_COUNT; i++)
		{
			if (lastButtons[i])
			{
				lastButtons[i] = false;
				gamepad_btn_input(i, false);
			}
		}
		lastAxes.fill(INT32_MIN);
		setRumble(0, 0);
	}

protected:
	std::shared_ptr<InputMapping> getDefaultMapping() override
	{
		// settings.platform.isArcade() is what gamepad_device.cpp uses to pick
		// the default for every other device, so match it.
		return std::make_shared<MisterInputMapping>(settings.platform.isArcade());
	}

private:
	void applyAxis(u32 code, int value)
	{
		if (lastAxes[code] == value)
			return;
		lastAxes[code] = value;
		gamepad_axis_input(code, value);
	}

	// State-change only. The core repeats the last value until replaced, so
	// resending every frame is both wasteful and wrong; 0,0 is what stops it.
	void setRumble(uint8_t strong, uint8_t weak)
	{
		if (lastRumble[0] == strong && lastRumble[1] == weak)
			return;
		lastRumble[0] = strong;
		lastRumble[1] = weak;
		sendRumble(player, strong, weak);
	}

	int player;
	std::array<bool, MB_COUNT> lastButtons {};
	std::array<int, MA_COUNT> lastAxes { { INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN } };
	std::array<uint8_t, 2> lastRumble { { 0, 0 } };
};

static std::array<std::shared_ptr<MisterGamepad>, 2> pads;
static bool registered = false;

void updateInputDevices()
{
	// Deliberately NOT gated on a live session - see groovy_input.h. The pads
	// must exist in the Controls tab so they can be mapped and assigned to a
	// maple port before the MiSTer is ever reachable.
	const bool wanted = config::GroovyEnable && config::GroovyUseInputs;

	if (!wanted)
	{
		termInputDevices();
		return;
	}

	if (!registered)
	{
		for (int i = 0; i < 2; i++)
		{
			pads[i] = std::make_shared<MisterGamepad>(i);
			// Restores this pad's saved maple port by unique_id, so a port
			// assignment made before connecting survives.
			GamepadDevice::Register(pads[i]);
		}
		registered = true;
		logAlways("MiSTer pads registered (available for mapping; input flows once connected)");
	}
}

void applyInputs()
{
	if (!registered)
		return;

	const MisterPadSnapshot& snap = padSnapshot();
	if (!snap.available)
		return;

	for (int i = 0; i < 2; i++)
		pads[i]->apply(snap);
}

void termInputDevices()
{
	if (!registered)
		return;
	for (int i = 0; i < 2; i++)
	{
		if (pads[i])
		{
			// Motors off before the device goes away. The core also force-stops
			// them on session close, so this is belt-and-braces for the case
			// where inputs are switched off with the session still up.
			pads[i]->resetState();
			GamepadDevice::Unregister(pads[i]);
			pads[i].reset();
		}
	}
	registered = false;
	logAlways("MiSTer pads unregistered");
}

} // namespace groovy
