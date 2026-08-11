/*
	Groovy MiSTer - MiSTer-side pads as flycast gamepads.

	Socket-free: consumes the snapshot groovy_output publishes, so the client
	header stays confined to groovy_output.cpp.

	The MiSTer's pads are presented as ordinary GamepadDevice instances so they
	flow through flycast's existing mapping, per-game bindings and rumble
	plumbing, rather than a bespoke path. On the wire the buttons are generic
	"Button 1..12" positions - every physical controller conforms via per-device
	.map files on the MiSTer itself, so there is never any per-controller
	configuration to do on this side.
*/
#pragma once

namespace groovy
{

/*
	Register or unregister the two MiSTer pad devices, driven purely by the
	"Use MiSTer Controllers" setting - NOT by whether a session is live.

	That distinction is the point: flycast's Controls tab enumerates registered
	GamepadDevices, so gating registration on an active connection would leave
	the pads invisible in the mapping UI until the MiSTer was up. Registering
	them up front means they can be assigned to a maple port and remapped before
	ever connecting, and GamepadDevice::Register() restores each one's saved
	maple port by unique_id, so those assignments persist.

	Call from the main loop, which runs whether or not a game is running.
*/
void updateInputDevices();

// Feed the latest MiSTer snapshot into the registered devices. Separate from
// registration because this one is determinism-sensitive: it must only run at
// the points flycast samples local input (see groovy::pollInputs).
void applyInputs();

// Drop the devices, e.g. when inputs are turned off or on shutdown.
void termInputDevices();

} // namespace groovy
