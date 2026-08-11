#pragma once

#include <mutex>
#include "rend/gui.h"
#include "cfg/cfg.h"
#include "imgui/imgui.h"
#include "rend/gles/imgui_impl_opengl3.h"
#include "rend/gles/gles.h"
#include "rend/gui_util.h"
#include "version.h"
#include "oslib/oslib.h"
#include "oslib/audiostream.h"
#include "log/LogManager.h"
#include "game_scanner.h"

#include "dojo/DojoGui.hpp"
#include "dojo/DojoFile.hpp"

#ifdef USE_GROOVY
// All socket-free: none of these pull groovymister.h / switchres_wrapper.h and
// therefore none pull <winsock2.h> or <windows.h>, which would otherwise
// conflict with the <windows.h> flycast already includes elsewhere.
#include <cstring>
#include <deque>
#include "rend/groovy/groovy_capture.h"
#include "rend/groovy/groovy_log.h"
#include "rend/groovy/groovy_output.h"
#include "rend/groovy/groovy_pixels.h"
#include "rend/groovy/groovy_switchres.h"
#endif

class GuiSettings
{
public:
    void settings_body_general(ImVec2 normal_padding);
    void settings_body_audio(ImVec2 normal_padding);
    void settings_body_advanced(ImVec2 normal_padding);
    void settings_body_about(ImVec2 normal_padding);
    void settings_body_video(ImVec2 normal_padding);
    void settings_body_credits(ImVec2 normal_padding);
    void settings_body_update(ImVec2 normal_padding);
#ifdef USE_GROOVY
    void settings_body_mister(ImVec2 normal_padding);
#endif

    std::string update_channel = "";
    std::string latest = "";
};