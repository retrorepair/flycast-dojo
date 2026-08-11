#!/usr/bin/env bash
#
# build-mingw-pinned.sh — build flycast-dojo with a PINNED toolchain.
#
# Why this exists
# ---------------
# flycast-dojo is a 2023-era codebase. Current msys2 ships CMake 4.x and GCC 16,
# and three things break:
#
#   1. CMake 4 removed support for cmake_minimum_required(VERSION < 3.5), which
#      27 vendored dependencies still declare.
#   2. CMake 4 removed CMP0026 OLD, which the bundled-zlib block in the root
#      CMakeLists.txt relies on.
#   3. GCC 14+ tightened transitive includes, so glslang's SPIRV/SpvBuilder.h
#      no longer gets <cstdint> for free and Vulkan will not compile.
#
# Rather than modify any of that, this script builds against a pinned CMake 3.31
# and GCC 13.2 kept OUTSIDE the repo. Nothing is installed and msys2's own
# /mingw64 is untouched, so every other project on the machine keeps building
# exactly as it does today.
#
# The build is as self-contained as it can be: SDL and Lua are built from source
# with the pinned compiler rather than taken from msys2. That matters because
# /mingw64/include carries msys2's mingw-w64 CRT headers (runtime v15) and the
# pinned toolchain ships its own (rt_v11); mixing them is the one genuinely
# dangerous failure mode here.
#
# msys2 is NOT used for any library: zlib comes from the pinned toolchain's own
# sysroot, SDL and Lua are built from source, and cpr builds its own curl. Any
# msys2 include reaching the compile line shadows the pinned C runtime headers
# and breaks the link, so the script audits for that after configuring.
#
# One exception: breakpad is off by default, because its Windows build target
# cannot be built against the older pinned runtime.
#
# Toolchain layout (override the root with $FLYCAST_TOOLCHAIN_ROOT):
#
#   /c/toolchains/flycast/
#     cmake-3.31.12/       cmake 3.31.x, extracted zip (bin/cmake.exe)
#     mingw64-13.2.0/      GCC 13.2, extracted 7z (bin/g++.exe)
#     lua-5.4.7/           built automatically on first run
#
# Runs from an msys2 MINGW64 shell or Git Bash - it needs neither.
#
set -euo pipefail

readonly CMAKE_REQUIRED_MAJOR=3
readonly GCC_REQUIRED_MAJOR=13
readonly GCC_DOWNLOAD_URL="https://github.com/niXman/mingw-builds-binaries/releases/download/13.2.0-rt_v11-rev1/x86_64-13.2.0-release-posix-seh-msvcrt-rt_v11-rev1.7z"
readonly CMAKE_DOWNLOAD_URL="https://github.com/Kitware/CMake/releases/download/v3.31.12/cmake-3.31.12-windows-x86_64.zip"

# Lua 5.4, built from source with the pinned compiler (see setup_lua).
#
# NOT msys2's lua, for two independent reasons:
#
#   1. Its headers sit directly in /mingw64/include, so find_package(Lua) would
#      set LUA_INCLUDE_DIR to that whole directory and flycast would compile with
#      -I C:/msys64/mingw64/include. That puts msys2's mingw-w64 CRT headers
#      (runtime v15) ahead of the pinned toolchain's own (rt_v11) - a four
#      major-version gap in _mingw.h, stdio.h and 1600 other headers.
#   2. msys2 currently ships Lua 5.5, whose API flycast has never been built
#      against. Upstream CI uses 5.3 (linux) and 5.4 (macOS), and luabridge
#      requires >= 5.2.
readonly LUA_VERSION="5.4.7"
readonly LUA_URL="https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"

TOOLCHAIN_ROOT="${FLYCAST_TOOLCHAIN_ROOT:-/c/toolchains/flycast}"
BUILD_DIR="build-mingw"
JOBS="$(nproc 2>/dev/null || echo 4)"
CLEAN=0
GROOVY=ON
CTEST=0
WANT_LUA=1
REBUILD_LUA=0
BREAKPAD=OFF
# Matches flycast's own CI, which ships RelWithDebInfo and strips afterwards.
BUILD_TYPE=RelWithDebInfo
STRIP=0
EXTRA_ARGS=()

# Repo root, resolved from this script's location so it can be run from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

die() { printf '\n[build-mingw-pinned] ERROR: %s\n\n' "$*" >&2; exit 1; }
note() { printf '[build-mingw-pinned] %s\n' "$*"; }

usage() {
	cat <<EOF
Usage: shell/windows/build-mingw-pinned.sh [options] [-- <extra cmake args>]

  --clean          remove the build directory first
  --no-groovy      build with -DUSE_GROOVY_MISTER=OFF (regression check:
                   proves the MiSTer feature is cleanly optional)
  --ctest          enable -DENABLE_CTEST=ON so the groovy unit tests build
  --breakpad       build breakpad crash reporting (default OFF here - the
                   pinned rt_v11 runtime cannot build it, see below)
  --no-lua         skip Lua entirely (training-mode scripting disabled)
  --rebuild-lua    force a rebuild of Lua even if it is already present
  --build-type T   Release | RelWithDebInfo (default) | Debug | MinSizeRel.
                   RelWithDebInfo is what flycast CI ships - same optimisation
                   as Release, plus debug symbols. It is NOT a debug build.
  --strip          strip symbols after linking, as CI does before packaging
                   (takes the binary from ~230 MB to ~12 MB)
  --jobs N         parallel build jobs (default: $JOBS)
  --build-dir DIR  build directory (default: $BUILD_DIR)
  -h, --help       this message

Lua $LUA_VERSION is fetched and built with the pinned compiler on first run,
into the toolchain root, and reused thereafter. msys2's lua cannot be used -
see the comment on LUA_VERSION for why.

Environment:
  FLYCAST_TOOLCHAIN_ROOT   pinned toolchain root (default: /c/toolchains/flycast)
EOF
}

validate_build_type() {
	case "$1" in
		Release|RelWithDebInfo|Debug|MinSizeRel) return 0 ;;
		*) die "unknown build type: $1
Valid: Release, RelWithDebInfo (default), Debug, MinSizeRel

Note RelWithDebInfo is NOT a debug build - it is -O2 with symbols kept, and is
what flycast's own CI ships. Use --strip to drop the symbols afterwards." ;;
	esac
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--clean)      CLEAN=1; shift ;;
		--no-groovy)  GROOVY=OFF; shift ;;
		--ctest)      CTEST=1; shift ;;
		--breakpad)   BREAKPAD=ON; shift ;;
		--build-type) BUILD_TYPE="${2:?--build-type needs a value}"; validate_build_type "$BUILD_TYPE"; shift 2 ;;
		--release)    BUILD_TYPE=Release; shift ;;
		--strip)      STRIP=1; shift ;;
		--no-lua)     WANT_LUA=0; shift ;;
		--rebuild-lua) REBUILD_LUA=1; shift ;;
		# Kept working because an earlier revision of this script documented it.
		--setup-lua)  REBUILD_LUA=1; shift ;;
		--jobs)       JOBS="${2:?--jobs needs a value}"; shift 2 ;;
		--build-dir)  BUILD_DIR="${2:?--build-dir needs a value}"; shift 2 ;;
		-h|--help)    usage; exit 0 ;;
		--)           shift; EXTRA_ARGS=("$@"); break ;;
		*)            usage; die "unknown option: $1" ;;
	esac
done

#
# Locate the pinned tools.
#
# mingw-builds' 7z unpacks to a mingw64/ directory, so accept both the
# already-flattened layout and the raw extraction.
#
find_first_existing() {
	local candidate
	for candidate in "$@"; do
		[[ -x "$candidate" ]] && { printf '%s' "$candidate"; return 0; }
	done
	return 1
}

CMAKE_BIN="$(find_first_existing \
	"$TOOLCHAIN_ROOT"/cmake-*/bin/cmake.exe \
	"$TOOLCHAIN_ROOT"/cmake/bin/cmake.exe \
	2>/dev/null || true)"

GXX_BIN="$(find_first_existing \
	"$TOOLCHAIN_ROOT"/mingw64-*/bin/g++.exe \
	"$TOOLCHAIN_ROOT"/mingw64-*/mingw64/bin/g++.exe \
	"$TOOLCHAIN_ROOT"/mingw64/bin/g++.exe \
	2>/dev/null || true)"

if [[ -z "$CMAKE_BIN" ]]; then
	die "no pinned CMake under $TOOLCHAIN_ROOT

Expected something like:
  $TOOLCHAIN_ROOT/cmake-3.31.12/bin/cmake.exe

Get it from:
  $CMAKE_DOWNLOAD_URL

(Or point FLYCAST_TOOLCHAIN_ROOT somewhere else.)
Deliberately NOT falling back to msys2's cmake: it is 4.x, which cannot
configure this project."
fi

if [[ -z "$GXX_BIN" ]]; then
	die "no pinned GCC under $TOOLCHAIN_ROOT

Expected something like:
  $TOOLCHAIN_ROOT/mingw64-13.2.0/bin/g++.exe

Get it from:
  $GCC_DOWNLOAD_URL
(the posix-seh-msvcrt variant — it matches msys2 MINGW64's runtime model)

Deliberately NOT falling back to msys2's gcc: it is 16.x, which cannot
compile glslang, so Vulkan would not build."
fi

CMAKE_DIR="$(cd "$(dirname "$CMAKE_BIN")" && pwd)"
GXX_DIR="$(cd "$(dirname "$GXX_BIN")" && pwd)"
GCC_BIN="$GXX_DIR/gcc.exe"

WINDRES_BIN="$GXX_DIR/windres.exe"
STRIP_BIN="$GXX_DIR/strip.exe"
# mingw-w64 sysroot: <toolchain>/x86_64-w64-mingw32/{include,lib}. This is
# where the toolchain keeps zlib and the C runtime, and it is what we point
# zlib at so msys2 is never consulted.
TOOLCHAIN_SYSROOT="$(cd "$GXX_DIR/.." && pwd)/x86_64-w64-mingw32"

# Checked here rather than left to CMake, which would otherwise fail much later
# inside its compiler-detection step with a far less obvious message.
[[ -x "$GCC_BIN" ]] || die "found g++ but no gcc alongside it:
  $GCC_BIN
The toolchain directory looks incomplete - re-extract it."

# Not fatal on its own, but if it is missing CMake silently falls back to
# msys2's, which cannot preprocess this toolchain's headers.
[[ -x "$WINDRES_BIN" ]] || die "found g++ but no windres alongside it:
  $WINDRES_BIN
The toolchain directory looks incomplete - re-extract it."

# Only needed for --strip, so checked lazily there rather than aborting a build
# that never uses it.

# zlib comes from here (see the configure args), so a toolchain without it would
# otherwise fail deep inside libchdr's find_package(ZLIB REQUIRED).
[[ -f "$TOOLCHAIN_SYSROOT/include/zlib.h" && -f "$TOOLCHAIN_SYSROOT/lib/libz.a" ]] || \
	die "pinned toolchain has no zlib:
  $TOOLCHAIN_SYSROOT/include/zlib.h
  $TOOLCHAIN_SYSROOT/lib/libz.a
The toolchain directory looks incomplete - re-extract it."

# Ahead of /mingw64/bin so the pinned tools win. mingw-builds ships its own
# mingw32-make, which the "MinGW Makefiles" generator needs, so putting the GCC
# bin dir first keeps make and compiler from the same toolchain.
export PATH="$GXX_DIR:$CMAKE_DIR:$PATH"

#
# Assert what we ACTUALLY got.
#
# This is the guard rail. Silently falling through to msys2's CMake 4.x is the
# exact failure this script exists to prevent, and it is easy to hit by running
# in a shell where the export did not apply.
#
# Interrogate the binaries we actually selected, not whatever PATH resolves.
# Asking `cmake` would depend on PATH lookup rules and could report a different
# binary than the one handed to the build.
#
# Use -dumpfullversion for the compiler rather than scraping --version: the
# banner format varies by vendor (mingw-builds says "13.2.0", Debian/Ubuntu
# cross builds say "13-win32"), and a regex demanding three dotted components
# silently fails on the latter.
cmake_version="$("$CMAKE_BIN" --version 2>/dev/null | head -1 | grep -oE '[0-9]+(\.[0-9]+)*' | head -1 || true)"
gxx_version="$("$GXX_BIN" -dumpfullversion 2>/dev/null || "$GXX_BIN" -dumpversion 2>/dev/null || true)"

[[ -n "$cmake_version" ]] || die "could not run the pinned cmake: $CMAKE_BIN"
[[ -n "$gxx_version" ]] || die "could not run the pinned g++: $GXX_BIN"

# Major version = leading digit run, so both "13.2.0" and "13-win32" parse.
gxx_major="${gxx_version%%[!0-9]*}"

if [[ "${cmake_version%%.*}" != "$CMAKE_REQUIRED_MAJOR" ]]; then
	die "pinned cmake is $cmake_version, need ${CMAKE_REQUIRED_MAJOR}.x
  $CMAKE_BIN
CMake 4 removed cmake_minimum_required(<3.5) and CMP0026 OLD, both of which
this project's vendored dependencies still need."
fi

if [[ "$gxx_major" != "$GCC_REQUIRED_MAJOR" ]]; then
	die "pinned g++ is $gxx_version, need ${GCC_REQUIRED_MAJOR}.x
  $GXX_BIN
GCC 14+ dropped the transitive <cstdint> include that glslang's
SPIRV/SpvBuilder.h depends on, so Vulkan will not compile."
fi

# Belt and braces: if PATH still resolves to something else, the generator or a
# sub-build could pick up the wrong tool even though we pinned the compilers in
# the cache. Warn rather than abort - it is not fatal, but it is worth seeing.
path_cmake="$(command -v cmake 2>/dev/null || true)"
if [[ -n "$path_cmake" && "$path_cmake" != "$CMAKE_BIN" ]]; then
	path_cmake_version="$("$path_cmake" --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo '?')"
	[[ "${path_cmake_version%%.*}" == "$CMAKE_REQUIRED_MAJOR" ]] || \
		note "WARNING: 'cmake' on PATH is $path_cmake_version at $path_cmake (using $CMAKE_BIN)"
fi

#
# Lua, built from source with the PINNED compiler so it links cleanly and, more
# importantly, so its headers live in a directory of their own rather than
# dragging msys2's whole /mingw64/include into the build.
#
LUA_PREFIX="$TOOLCHAIN_ROOT/lua-$LUA_VERSION"
LUA_INCLUDE_DIR="$LUA_PREFIX/include"
LUA_LIBRARY="$LUA_PREFIX/lib/liblua.a"

setup_lua() {
	note "building Lua $LUA_VERSION with the pinned compiler..."
	local work
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' RETURN

	note "  fetching $LUA_URL"
	curl -fsSL "$LUA_URL" -o "$work/lua.tar.gz" \
		|| die "could not download Lua from $LUA_URL"
	tar -xzf "$work/lua.tar.gz" -C "$work" \
		|| die "could not extract the Lua tarball"

	local src="$work/lua-$LUA_VERSION/src"
	[[ -f "$src/lua.h" ]] || die "unexpected Lua tarball layout: no $src/lua.h"

	# Compile every translation unit except the two that define main()
	# (the standalone interpreter and the bytecode compiler).
	local objs=()
	local c obj
	for c in "$src"/*.c; do
		case "$(basename "$c")" in
			lua.c|luac.c) continue ;;
		esac
		obj="${c%.c}.o"
		"$GCC_BIN" -O2 -DNDEBUG -c "$c" -o "$obj" -I"$src" \
			|| die "failed compiling $(basename "$c")"
		objs+=("$obj")
	done

	mkdir -p "$LUA_INCLUDE_DIR" "$LUA_PREFIX/lib"
	"$GXX_DIR/ar.exe" rcs "$LUA_LIBRARY" "${objs[@]}" \
		|| die "failed creating $LUA_LIBRARY"
	cp "$src"/lua.h "$src"/luaconf.h "$src"/lualib.h "$src"/lauxlib.h "$LUA_INCLUDE_DIR/"
	[[ -f "$src/lua.hpp" ]] && cp "$src/lua.hpp" "$LUA_INCLUDE_DIR/"

	note "  installed to $LUA_PREFIX"
}

# Built on demand rather than behind an opt-in flag: flycast enables Lua
# whenever find_package(Lua) succeeds, so on every other platform it is simply
# present. Requiring a flag here made a build that silently differs from CI the
# default, which is the wrong way round. The build already downloads
# dependencies (cpr fetches curl), so one 350KB tarball is in keeping.
lua_present() { [[ -f "$LUA_INCLUDE_DIR/lua.h" && -f "$LUA_LIBRARY" ]]; }

if [[ "$WANT_LUA" -eq 1 ]]; then
	if [[ "$REBUILD_LUA" -eq 1 ]] || ! lua_present; then
		setup_lua
	fi
fi

LUA_AVAILABLE=0
if [[ "$WANT_LUA" -eq 1 ]] && lua_present; then
	LUA_AVAILABLE=1
fi

#
# msys2 prefix, for the two libraries we do not build ourselves (curl, OpenSSL).
#
# Deliberately /mingw64 rather than $MINGW_PREFIX: the pinned toolchain is the
# msvcrt variant, and MINGW64 is msys2's msvcrt environment. Taking whatever
# environment the shell happens to be (UCRT64, CLANG64) would pair a msvcrt
# compiler with UCRT-linked import libraries.
#
# Paths handed to cmake must be WINDOWS paths. The pinned cmake is a native
# binary and does not understand MSYS-style "/c/..." - passing one silently
# matches nothing, and the resulting failure surfaces hundreds of lines later
# as an unrelated-looking "not found".
if command -v cygpath >/dev/null 2>&1; then
	REPO_ROOT_WIN="$(cygpath -m "$REPO_ROOT")"
	TOOLCHAIN_SYSROOT_WIN="$(cygpath -m "$TOOLCHAIN_SYSROOT")"
else
	REPO_ROOT_WIN="$REPO_ROOT"
	TOOLCHAIN_SYSROOT_WIN="$TOOLCHAIN_SYSROOT"
fi

note "repo:   $REPO_ROOT"
note "cmake:  $CMAKE_BIN  ($cmake_version)"
note "g++:    $GXX_BIN  ($gxx_version)"
note "make:   $(command -v mingw32-make 2>/dev/null || echo '<not found - generator will look for it on PATH>')"
if [[ "$LUA_AVAILABLE" -eq 1 ]]; then
	note "lua:    $LUA_LIBRARY  ($LUA_VERSION)"
else
	note "lua:    DISABLED (--no-lua) - training-mode Lua scripting unavailable."
fi
note "build:  $BUILD_DIR   $BUILD_TYPE   groovy=$GROOVY  breakpad=$BREAKPAD  jobs=$JOBS"

cd "$REPO_ROOT"

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
	note "removing $BUILD_DIR"
	rm -rf "$BUILD_DIR"
fi

configure_args=(
	-B "$BUILD_DIR"
	-G "MinGW Makefiles"
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE"
	-DCMAKE_INSTALL_PREFIX=artifact

	# Pin the compilers explicitly rather than relying on PATH alone, so the
	# choice is recorded in the cache and a later reconfigure cannot drift.
	-DCMAKE_C_COMPILER="$GCC_BIN"
	-DCMAKE_CXX_COMPILER="$GXX_BIN"

	# windres too, and this one is not optional. Left to itself CMake finds
	# msys2's windres, which runs msys2's GCC 16 preprocessor over headers from
	# the pinned rt_v11 toolchain and dies with a bare:
	#
	#   C:\msys64\mingw64\bin\windres.exe: preprocessing failed.
	#
	# ...at 100%, on the very last object, having compiled everything else.
	-DCMAKE_RC_COMPILER="$WINDRES_BIN"

	# Self-contained: build the SDL submodule instead of linking msys2's, which
	# is what keeps /mingw64/include (GCC 16 CRT headers) out of a GCC 13 build.
	-DUSE_HOST_SDL=OFF

	# Let cpr build its own curl, against Windows Schannel.
	#
	# Using msys2's curl instead looks simpler but is not survivable: CMake
	# resolves it through msys2's CURLConfig.cmake, which pulls curl's whole
	# dependency chain (brotli, libidn2, nghttp2, psl, ssh2, zstd) via
	# pkg-config, and every one of them contributes
	# -IC:/msys64/mingw64/include to flycast's compile line. That shadows the
	# pinned toolchain's C runtime headers and the link then fails with
	# undefined pthread_cond_timedwait64 / nanosleep64. Naming
	# CURL_INCLUDE_DIR/CURL_LIBRARY does not prevent it, because the package
	# config wins over the Find module.
	#
	# This was originally avoided because cpr unconditionally FetchContents
	# zlib-ng as a target named "zlib", colliding with the one libchdr bundles.
	# The zlib pinning below removes that collision at its root: with
	# WITH_SYSTEM_ZLIB=ON, libchdr calls find_package(ZLIB) instead of
	# add_library(zlib), so only one "zlib" target is ever created.
	-DCPR_FORCE_USE_SYSTEM_CURL=OFF
	-DCPR_FORCE_WINSSL_BACKEND=ON
	-DCPR_BUILD_TESTS=OFF
	-DCPR_BUILD_TESTS_SSL=OFF
	-DCURL_ZLIB=OFF

	# Turn off every OPTIONAL curl dependency, or curl auto-detects msys2's
	# copies and adds -IC:/msys64/mingw64/include to its own compile line. That
	# shadows curl's bundled headers with msys2's, and curl fails to build
	# itself:
	#
	#   _deps/curl-src/lib/vtls/vtls.c:74: error: invalid use of undefined type
	#     'struct Curl_share'
	#
	# libssh2 is the one that actually bit (flycast's own macOS CI passes
	# -DCMAKE_USE_LIBSSH2=OFF for the same reason); the rest are pre-emptive,
	# since any of them would do the same if installed. cpr only needs HTTP(S),
	# which HTTP_ONLY already restricts it to.
	-DCMAKE_USE_LIBSSH2=OFF
	-DCMAKE_USE_LIBSSH=OFF
	-DCURL_USE_LIBSSH2=OFF
	-DCURL_USE_LIBSSH=OFF
	-DCURL_BROTLI=OFF
	-DCURL_ZSTD=OFF
	-DUSE_NGHTTP2=OFF
	-DUSE_LIBIDN2=OFF
	-DCURL_USE_LIBPSL=OFF
	-DCURL_DISABLE_LDAP=ON
	-DCURL_DISABLE_LDAPS=ON

	# Point ONLY the library lookups at msys2, never the header search.
	#
	# CMAKE_PREFIX_PATH would do both, and that is not survivable: it puts
	# msys2's /mingw64/include ahead of the pinned toolchain's own, so flycast
	# compiles against v15 headers and links rt_v11 libraries. It fails at the
	# very last step, after a full successful compile, as:
	#
	#   objects.a(emulator.cpp.obj): in function `pthread_cond_timedwait':
	#   C:/msys64/mingw64/include/pthread.h:321:
	#     undefined reference to `pthread_cond_timedwait64'
	#
	# ...because v15's pthread.h is an inline shim dispatching to *64 symbols
	# that rt_v11's libwinpthread does not export (it exports the plain names).
	# Same story for nanosleep64 and the stat64i32 family.
	#
	# zlib from the PINNED TOOLCHAIN, which ships both zlib.h and libz.a.
	#
	# This one setting resolves two problems at once:
	#
	#  1. Left alone, find_package(ZLIB) at CMakeLists.txt:440 finds msys2's and
	#     caches ZLIB_INCLUDE_DIR=C:/msys64/mingw64/include. flycast cannot undo
	#     that: :441 correctly skips linking it on Windows, and :465 tries to
	#     point the variable back in-tree - but a plain set() cannot overwrite an
	#     existing CACHE entry, so msys2's value survives and reaches libzip and
	#     flycast's own target.
	#  2. WITH_SYSTEM_ZLIB=ON makes libchdr call find_package(ZLIB) instead of
	#     add_subdirectory(deps/zlib-1.2.12), so NO target named "zlib" is
	#     created - which is what previously collided with the zlib-ng that cpr
	#     FetchContents for curl. That collision is why this build originally
	#     retreated to msys2's curl; removing it lets cpr build curl itself and
	#     takes msys2 out of the build completely.
	-DWITH_SYSTEM_ZLIB=ON
	-DZLIB_INCLUDE_DIR="$TOOLCHAIN_SYSROOT_WIN/include"
	-DZLIB_LIBRARY="$TOOLCHAIN_SYSROOT_WIN/lib/libz.a"

	# asio from the vendored copy.
	#
	# CMakeLists.txt adds core/dojo/deps/asio-1.22.0/include only for ANDROID or
	# APPLE; every other platform expects asio from the system, which is why CI
	# installs mingw-w64-x86_64-asio / libasio-dev / brew asio. With msys2 off the
	# include path there is nothing to find and DojoSession.hpp fails on
	# <asio.hpp>.
	#
	# The vendored copy is also the more correct choice: it is 1.22.0, matching
	# what this code was written against, while msys2 currently ships 1.38.0 - so
	# every build before this one compiled dojo's netplay against an asio 16
	# releases newer than intended. It is header-only, so this is purely an
	# include path with nothing to link.
	-DUSE_BUNDLED_ASIO=ON

	-DUSE_GROOVY_MISTER="$GROOVY"

	# Off by default here, unlike every other flycast build.
	#
	# core/deps/breakpad's Windows branch runs a bare `mingw32-make` rather than
	# passing --disable-processor --disable-tools and building only
	# libbreakpad_client.a, which is what its own non-Windows branch does. So it
	# builds two components flycast never links, and both fail here:
	#
	#   src/common/linux/symbol_collector_client.cc  -> needs curl/curl.h, which
	#     is not on the pinned toolchain's default include path
	#   src/processor/microdump_stackwalk.exe        -> needs an underscore-less
	#     `stat64i32`; the pinned rt_v11 runtime exports only `_stat64i32`,
	#     whereas msys2's rt_v15 exports both
	#
	# Pass --breakpad to try anyway. The real fix belongs upstream in
	# flyinghead/mingw-breakpad - see docs/UPSTREAM_REPORT_mingw_breakpad_build_all.md
	-DUSE_BREAKPAD="$BREAKPAD"
)

# Note there is no -DUSE_VULKAN=OFF, no -DCMAKE_POLICY_VERSION_MINIMUM and no
# -DWITH_SYSTEM_ZLIB here. Pinning the toolchain is what makes all three
# workarounds unnecessary: GCC 13 compiles glslang, and CMake 3.31 still honours
# the old policies.

if [[ "$LUA_AVAILABLE" -eq 1 ]]; then
	# Pre-seeding these short-circuits FindLua's find_path/find_library, so it
	# never searches /mingw64 and LUA_INCLUDE_DIR stays a directory containing
	# only Lua's five headers.
	configure_args+=(
		-DLUA_INCLUDE_DIR="$LUA_INCLUDE_DIR"
		-DLUA_LIBRARY="$LUA_LIBRARY"
	)
fi

# ENABLE_CTEST brings in include(CTest), which is what registers add_test() and
# builds the groovy unit tests. BUILD_TESTING must be forced OFF alongside it:
# include(CTest) defaults it ON, and CMakeLists.txt:1802 then compiles gtest AND
# flycast's own test suite INTO the flycast-dojo target. That is wrong here for
# two independent reasons:
#
#   1. core/deps/gtest/src/gtest_main.cc defines its own int main(), so the
#      product stops being the emulator and becomes a unit-test runner.
#   2. That gtest copy predates GCC 13's tighter transitive includes and fails
#      with 31 "'std::intmax_t' has not been declared" errors. (Verified: adding
#      #include <cstdint> to the three files using it clears all 31 - a separate
#      fix, not needed for this build.)
#
# The groovy test target is guarded on ENABLE_CTEST, not BUILD_TESTING, so it
# still builds and still registers with ctest.
if [[ "$CTEST" -eq 1 ]]; then
	configure_args+=(-DENABLE_CTEST=ON -DBUILD_TESTING=OFF)
fi
[[ ${#EXTRA_ARGS[@]} -gt 0 ]] && configure_args+=("${EXTRA_ARGS[@]}")

# Invoke the pinned cmake by absolute path. PATH is still prepended above (the
# generator and sub-builds need the toolchain on it), but the binary WE run is
# never left to PATH resolution - using the wrong cmake silently is the exact
# failure this script exists to prevent.
note "configuring..."
"$CMAKE_BIN" "${configure_args[@]}"

#
# Post-configure audit: msys2's include dir must not be on ANY target's compile
# line.
#
# This is the failure that cost several full build cycles. It is silent at
# configure time and survives a complete 100% compile, only to surface at the
# final link as a wall of:
#
#   undefined reference to `pthread_cond_timedwait64'
#   undefined reference to `nanosleep64'
#
# ...because msys2's headers (mingw-w64 runtime v15) inline-dispatch to *64
# symbols that the pinned rt_v11 libraries do not export. -I is searched ahead
# of the compiler's built-in dirs, so a single stray -I C:/msys64/mingw64/include
# silently replaces the entire C runtime header set.
#
# Any dependency resolved from msys2 can reintroduce it (curl and zlib both did,
# by different routes), so check the generated flags rather than trying to
# enumerate the causes.
leaked=""
if [[ -z "${FLYCAST_ALLOW_MSYS2_INCLUDES:-}" ]]; then
	leaked="$(grep -rhoE -- "-I ?[\"']?[A-Za-z]:[\\\\/]msys64[^ \"']*" "$BUILD_DIR" \
		--include=flags.make --include='*.rsp' 2>/dev/null | sort -u || true)"
fi
if [[ -n "$leaked" ]]; then
	# Abort rather than warn. A warning here scrolls past in a 20-minute build
	# and the consequence does not surface until the final link, so the whole
	# cycle is wasted. Better to lose 30 seconds now.
	#
	# The offending target names the culprit precisely, which is faster than
	# reading CMakeCache.txt top to bottom:
	offenders="$(grep -rlE "msys64" "$BUILD_DIR" --include=flags.make --include='*.rsp' 2>/dev/null | head -5 || true)"
	[[ -n "$offenders" ]] || offenders="(none matched - the include may come from a .rsp not yet written)"
	die "msys2 include directories reached the compile line:

$(printf '  %s\n' "$leaked")

Affected target(s):
$(printf '  %s\n' "$offenders")

This shadows the pinned toolchain's C runtime headers. Left alone it either
breaks a dependency's own build, or compiles cleanly and then fails at link
with undefined pthread_cond_timedwait64 / nanosleep64 / stat64i32.

Find the cache variable that introduced it:
  grep -nE '=.*msys64.*include' $BUILD_DIR/CMakeCache.txt | grep -v ':_'

then pin that variable in this script's configure_args (see the zlib and Lua
entries for the pattern). Re-run with --clean afterwards.

To build anyway (not recommended): FLYCAST_ALLOW_MSYS2_INCLUDES=1"
fi

note "building with $JOBS job(s)..."
"$CMAKE_BIN" --build "$BUILD_DIR" -j "$JOBS"

if [[ "$STRIP" -eq 1 ]]; then
	# What CI does before packaging: dump_syms first (so crashes stay
	# symbolicated via breakpad), then strip. We skip dump_syms because
	# breakpad is off in this build by default - so strip here discards the
	# symbols rather than archiving them. Keep an unstripped copy if you expect
	# to debug a crash from this binary.
	exe="$BUILD_DIR/flycast-dojo.exe"
	[[ -x "$STRIP_BIN" ]] || die "--strip requested but no strip in the toolchain:
  $STRIP_BIN"
	if [[ -f "$exe" ]]; then
		before="$(du -m "$exe" 2>/dev/null | cut -f1)"
		"$STRIP_BIN" "$exe" || die "strip failed on $exe"
		after="$(du -m "$exe" 2>/dev/null | cut -f1)"
		note "stripped: ${before}MB -> ${after}MB"
	else
		note "WARNING: --strip requested but $exe not found"
	fi
fi

note "done. binary should be under $BUILD_DIR/"
if [[ "$CTEST" -eq 1 ]]; then
	note "run the groovy unit tests with:"
	note "  ctest --test-dir $BUILD_DIR -R groovy -V"
fi
