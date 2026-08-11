# switchres 2.2.1 (github.com/antonioginer/switchres), vendored calc-only: the
# modeline maths without any of the host display-switching backends. Only the
# sources below are imported; display_{linux,windows,sdl2} and the
# custom_video_* backends are deliberately absent, since SR_CALC_ONLY compiles
# them out anyway. A few files carry small MSVC/portability patches, marked in
# place.
add_library(switchres STATIC
	switchres.cpp
	monitor.cpp
	modeline.cpp
	display.cpp
	custom_video.cpp
	log.cpp
	edid.cpp
	switchres_wrapper.cpp
)

# SR_CALC_ONLY: build switchres as a pure modeline *calculator*. It drops every
# real host-display backend (display_linux / display_windows / display_sdl2 and
# their X11 / DRM / ADL / ATI custom_video dependencies) and keeps only the
# "dummy" backend. We never switch the host's display — the computed modeline is
# sent to the MiSTer over UDP. Do not remove this; without it switchres links
# display-switching machinery that could reprogram the user's own monitor.
#
# SR_WIN32_STATIC: non-dllexport wrapper symbols, for static linking. Also stops
# switchres_wrapper.h from emitting a LIBERROR() function body into every
# translation unit that includes it.
target_compile_definitions(switchres PUBLIC
	SR_CALC_ONLY
	SR_WIN32_STATIC
)

target_include_directories(switchres SYSTEM PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

# C++11 is sufficient — verified by compiling the whole calc-only set with
# -std=c++11 (gcc 13). Stated explicitly rather than inherited so a future bump
# of the main target does not silently change what this third-party code is
# built as.
target_compile_features(switchres PRIVATE cxx_std_11)

# Third-party code we do not want to churn. flycast has no
# disable_compiler_warnings_for_target() helper (that is a PCSX2 thing), so do it
# directly.
if(MSVC)
	target_compile_options(switchres PRIVATE /W0)
else()
	target_compile_options(switchres PRIVATE -w)
endif()
