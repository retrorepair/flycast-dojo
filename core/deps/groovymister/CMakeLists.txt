# The maintained Groovy MiSTer UDP client (Groovy_MiSTer, api/), vendored
# verbatim - no local changes, so a re-sync is a plain file copy. The flat C
# wrapper (groovymister_wrapper.*) is deliberately not imported; we link the
# C++ class directly.
#
# nlc_codec.* is required even when NLC is never enabled, because
# groovymister.cpp includes it unconditionally. rio.h supplies Windows
# Registered I/O declarations for toolchains whose SDK lacks them and is
# self-guarding, so it is harmless on every platform.
#
# lz4 is built from the bundled copy because flycast links LZ4 nowhere else, so
# GM_SYSTEM_LZ4 is deliberately NOT defined and groovymister.cpp takes its
# #include "lz4/lz4.h" branch, which matches this layout as-is.
#
# Only lz4.c and lz4hc.c are compiled. lz4/xxhash.c is deliberately NOT built:
# nothing we use references it (verified — lz4.c and lz4hc.c have zero XXH
# references), and building it would define XXH32/XXH64 a second time alongside
# flycast's own xxHash::xxhash (CMakeLists.txt:348).
add_library(groovymister STATIC
	groovymister.cpp
	nlc_codec.cpp
	lz4/lz4.c
	lz4/lz4hc.c
)

target_include_directories(groovymister SYSTEM PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

# C++11 is sufficient — verified by compiling groovymister.cpp and nlc_codec.cpp
# with -std=c++11 (gcc 13). No exceptions, no RTTI required.
target_compile_features(groovymister PRIVATE c_std_11 cxx_std_11)

if(WIN32)
	# winsock2 + RIO. RIO entry points are fetched at runtime via WSAIoctl, so
	# there is nothing else to link.
	target_link_libraries(groovymister PUBLIC ws2_32)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(groovymister PUBLIC m Threads::Threads)
endif()

# Third-party code we do not want to churn.
if(MSVC)
	target_compile_options(groovymister PRIVATE /W0)
else()
	target_compile_options(groovymister PRIVATE -w)
endif()
