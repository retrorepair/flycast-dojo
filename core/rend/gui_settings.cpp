#pragma once
#include "gui_settings.h"

inline static void header(const char *title)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f)); // Left
	ImGui::ButtonEx(title, ImVec2(-1, 0), ImGuiButtonFlags_Disabled);
	ImGui::PopStyleVar();
}

void GuiSettings::settings_body_general(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	const char *languages[] = {"Japanese", "English", "German", "French", "Spanish", "Italian", "Default"};
	OptionComboBox("Language", config::Language, languages, ARRAY_SIZE(languages),
				   "The language as configured in the Dreamcast BIOS");

	const char *broadcast[] = {"NTSC", "PAL", "PAL/M", "PAL/N", "Default"};
	OptionComboBox("Broadcast", config::Broadcast, broadcast, ARRAY_SIZE(broadcast),
				   "TV broadcasting standard for non-VGA modes");

	const char *region[] = {"Japan", "USA", "Europe", "Default"};
	OptionComboBox("Region", config::Region, region, ARRAY_SIZE(region),
				   "BIOS region");

	const char *cable[] = {"VGA", "RGB Component", "TV Composite"};
	{
		DisabledScope scope(config::Cable.isReadOnly());

		const char *value = config::Cable == 0 ? cable[0]
				: config::Cable > 0 && config::Cable <= ARRAY_SIZE(cable) ? cable[config::Cable - 1]
				: "?";
		if (ImGui::BeginCombo("Cable", value, ImGuiComboFlags_None))
		{
			for (int i = 0; i < IM_ARRAYSIZE(cable); i++)
			{
				bool is_selected = i == 0 ? config::Cable <= 1 : config::Cable - 1 == i;
				if (ImGui::Selectable(cable[i], &is_selected))
					config::Cable = i == 0 ? 0 : i + 1;
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}
	ImGui::SameLine();
	ShowHelpMarker("Video connection type");

#if !defined(TARGET_IPHONE)
	ImVec2 size;
	size.x = 0.0f;
	size.y = (ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f) * (config::ContentPath.get().size() + 1); //+ ImGui::GetStyle().FramePadding.y * 2.f;

	if (ImGui::ListBoxHeader("Content Location", size))
	{
		int to_delete = -1;
		for (u32 i = 0; i < config::ContentPath.get().size(); i++)
		{
			ImGui::PushID(config::ContentPath.get()[i].c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s", config::ContentPath.get()[i].c_str());
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("X").x - ImGui::GetStyle().FramePadding.x);
			if (ImGui::Button("X"))
				to_delete = i;
			ImGui::PopID();
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(24, 3));
		if (ImGui::Button("Add"))
			ImGui::OpenPopup("Select Directory");
		select_file_popup("Select Directory", [](bool cancelled, std::string selection)
						  {
                			if (!cancelled)
                			{
                				scanner_stop();
                				config::ContentPath.get().push_back(selection);
                				scanner_refresh();
                			}
                			return true; });
		ImGui::PopStyleVar();
		scrollWhenDraggingOnVoid();

		ImGui::ListBoxFooter();
		if (to_delete >= 0)
		{
			scanner_stop();
			config::ContentPath.get().erase(config::ContentPath.get().begin() + to_delete);
			scanner_refresh();
		}
	}
	ImGui::SameLine();
	ShowHelpMarker("The directories where your games are stored");

#if defined(__linux__) && !defined(__ANDROID__)
	if (ImGui::ListBoxHeader("Data Directory", 1))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", get_writable_data_path("").c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Open").x - ImGui::GetStyle().FramePadding.x);
		if (ImGui::Button("Open"))
		{
		    char temp[512];
		    sprintf(temp, "xdg-open \"%s\"", get_writable_data_path("").c_str());
		    system(temp);
		}
		ImGui::ListBoxFooter();
	}
	ImGui::SameLine();
	ShowHelpMarker("The directory containing BIOS files, as well as saved VMUs and states");
	if (ImGui::ListBoxHeader("Config Directory", 1))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", get_writable_config_path("").c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Open").x - ImGui::GetStyle().FramePadding.x);
		if (ImGui::Button("Open"))
		{
		    char temp[512];
		    sprintf(temp, "xdg-open \"%s\"", get_writable_config_path("").c_str());
		    system(temp);
		}
		ImGui::ListBoxFooter();
	}
	ImGui::SameLine();
	ShowHelpMarker("The directory where Flycast saves configuration files and controller mappings");
#else
#ifdef __ANDROID__
	if (ImGui::ListBoxHeader("Home Directory", 2))
#elif defined(__WIN32)
	ImVec2 home_size;
	home_size.x = 0.0f;
	home_size.y = (ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f) * 2;

	if (ImGui::ListBoxHeader("Home Directory", home_size))
#else
	if (ImGui::ListBoxHeader("Home Directory", 1))
#endif
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", get_writable_config_path("").c_str());
#ifdef __ANDROID__
		//ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Change").x - ImGui::GetStyle().FramePadding.x);
		if (ImGui::Button("Change"))
			gui_state = GuiState::Onboarding;
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Reveal in Finder").x - ImGui::GetStyle().FramePadding.x);
		if (ImGui::Button("Reveal in Finder"))
		{
		    char temp[512];
		    sprintf(temp, "open \"%s\"", get_writable_config_path("").c_str());
		    system(temp);
		}
#elif defined(__WIN32)
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(24, 3));
		if (ImGui::Button("Open"))
			ShellExecute(0, "open", get_writable_config_path("").c_str(), 0, 0 , SW_SHOW );
		ImGui::PopStyleVar();
#endif
		ImGui::ListBoxFooter();
	}
	ImGui::SameLine();
	ShowHelpMarker("The directory where Flycast saves configuration files and VMUs. BIOS files should be in a subfolder named \"data\"");
#endif // !linux
#endif // !TARGET_IPHONE

	OptionCheckbox("Box Art Game List", config::BoxartDisplayMode,
			"Display game covert art in the game list.");
	OptionCheckbox("Fetch Box Art", config::FetchBoxart,
			"Fetch cover images from TheGamesDB.net.");
	if (OptionCheckbox("Hide Legacy Naomi Roms", config::HideLegacyNaomiRoms,
					   "Hide .bin, .dat and .lst files from the content browser"))
		scanner_refresh();
	ImGui::Text("Automatic State:");
	OptionCheckbox("Load", config::AutoLoadState,
				   "Load the last saved state of the game when starting");
	ImGui::SameLine();
	OptionCheckbox("Save", config::AutoSaveState,
				   "Save the state of the game when stopping");
	OptionCheckbox("Naomi Free Play", config::ForceFreePlay, "Configure Naomi games in Free Play mode.");

	ImGui::PopStyleVar();
}

void GuiSettings::settings_body_audio(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	OptionCheckbox("Audio Frame Sync", config::LimitFPS, "Sync frames with audio output");
	OptionCheckbox("Enable DSP", config::DSPEnabled,
				   "Enable the Dreamcast Digital Sound Processor. Only recommended on fast platforms");
	if (OptionSlider("Volume Level", config::AudioVolume, 0, 100, "Adjust the emulator's audio level"))
	{
		config::AudioVolume.calcDbPower();
	};
#ifdef __ANDROID__
	if (config::AudioBackend.get() == "auto" || config::AudioBackend.get() == "android")
		OptionCheckbox("Automatic Latency", config::AutoLatency,
					   "Automatically set audio latency. Recommended");
#endif
	if (!config::AutoLatency || (config::AudioBackend.get() != "auto" && config::AudioBackend.get() != "android"))
	{
		int latency = (int)roundf(config::AudioBufferSize * 1000.f / 44100.f);
		ImGui::SliderInt("Latency", &latency, 12, 512, "%d ms");
		config::AudioBufferSize = (int)roundf(latency * 44100.f / 1000.f);
		ImGui::SameLine();
		ShowHelpMarker("Sets the maximum audio latency. Not supported by all audio drivers.");
	}

	AudioBackend *backend = nullptr;
	std::string backend_name = config::AudioBackend;
	if (backend_name != "auto")
	{
		backend = AudioBackend::getBackend(config::AudioBackend);
		if (backend != NULL)
			backend_name = backend->slug;
	}

	AudioBackend *current_backend = backend;
	if (ImGui::BeginCombo("Audio Driver", backend_name.c_str(), ImGuiComboFlags_None))
	{
		bool is_selected = (config::AudioBackend.get() == "auto");
		if (ImGui::Selectable("auto - Automatic driver selection", &is_selected))
			config::AudioBackend.set("auto");

		for (u32 i = 0; i < AudioBackend::getCount(); i++)
		{
			backend = AudioBackend::getBackend(i);
			is_selected = (config::AudioBackend.get() == backend->slug);

			if (is_selected)
				current_backend = backend;

			if (ImGui::Selectable((backend->slug + " - " + backend->name).c_str(), &is_selected))
				config::AudioBackend.set(backend->slug);
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ShowHelpMarker("The audio driver to use");

	if (current_backend != nullptr)
	{
		// get backend specific options
		int option_count;
		const AudioBackend::Option *options = current_backend->getOptions(&option_count);

		for (int o = 0; o < option_count; o++)
		{
			std::string value = cfgLoadStr(current_backend->slug, options->name, "");

			if (options->type == AudioBackend::Option::integer)
			{
				int val = stoi(value);
				if (ImGui::SliderInt(options->caption.c_str(), &val, options->minValue, options->maxValue))
				{
					std::string s = std::to_string(val);
					cfgSaveStr(current_backend->slug, options->name, s);
				}
			}
			else if (options->type == AudioBackend::Option::checkbox)
			{
				bool check = value == "1";
				if (ImGui::Checkbox(options->caption.c_str(), &check))
					cfgSaveStr(current_backend->slug, options->name,
							   check ? "1" : "0");
			}
			else if (options->type == AudioBackend::Option::list)
			{
				if (ImGui::BeginCombo(options->caption.c_str(), value.c_str(), ImGuiComboFlags_None))
				{
					bool is_selected = false;
					for (const auto &cur : options->values)
					{
						is_selected = value == cur;
						if (ImGui::Selectable(cur.c_str(), &is_selected))
							cfgSaveStr(current_backend->slug, options->name, cur);

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				WARN_LOG(RENDERER, "Unknown option");
			}

			options++;
		}
	}

	ImGui::PopStyleVar();
}

void GuiSettings::settings_body_advanced(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	header("CPU Mode");
	{
		ImGui::Columns(2, "cpu_modes", false);
		OptionRadioButton("Dynarec", config::DynarecEnabled, true,
						  "Use the dynamic recompiler. Recommended in most cases");
		ImGui::NextColumn();
		OptionRadioButton("Interpreter", config::DynarecEnabled, false,
						  "Use the interpreter. Very slow but may help in case of a dynarec problem");
		ImGui::Columns(1, NULL, false);
	}
	if (config::DynarecEnabled)
	{
		ImGui::Spacing();
		header("Dynarec Options");
		OptionCheckbox("Idle Skip", config::DynarecIdleSkip, "Skip wait loops. Recommended");
	}
	ImGui::Spacing();
	header("Network");
	{
		{
			DisabledScope scope(game_started);

			OptionCheckbox("Broadband Adapter Emulation", config::EmulateBBA,
						   "Emulate the Ethernet Broadband Adapter (BBA) instead of the Modem");
		}
		OptionCheckbox("Enable Naomi Networking", config::NetworkEnable,
					   "Enable networking for supported Naomi games");
		if (config::NetworkEnable)
		{
			OptionCheckbox("Act as Server", config::ActAsServer,
						   "Create a local server for Naomi network games");
			if (!config::ActAsServer)
			{
				char server_name[256];
				strcpy(server_name, config::NetworkServer.get().c_str());
				ImGui::InputText("Server", server_name, sizeof(server_name), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				ImGui::SameLine();
				ShowHelpMarker("The server to connect to. Leave blank to find a server automatically on the default port");
				config::NetworkServer.set(server_name);
			}
			char localPort[256];
			sprintf(localPort, "%d", (int)config::LocalPort);
			ImGui::InputText("Local Port", localPort, sizeof(localPort), ImGuiInputTextFlags_CharsDecimal, nullptr, nullptr);
			ImGui::SameLine();
			ShowHelpMarker("The local UDP port to use");
			config::LocalPort.set(atoi(localPort));
		}
		OptionCheckbox("Enable UPnP", config::EnableUPnP);
		ImGui::SameLine();
		ShowHelpMarker("Automatically configure your network router for netplay");
		OptionCheckbox("Broadcast Digital Outputs", config::NetworkOutput, "Broadcast digital outputs and force-feedback state on TCP port 8000. "
						"Compatible with the \"-output network\" MAME option. Arcade games only.");
	}
	ImGui::Spacing();

	header("Text Output");
	{
		OptionCheckbox("Output Session Details to Text Files", config::OutputStreamTxt, "Outputs in-game overlay details to external text files (in the 'out' folder). Useful for online streams.");
		if (config::OutputStreamTxt)
		{
			OptionCheckbox("Custom Output Data", config::CustomOutputStreamTxt, "Exports custom memory values by game. Definitions by game name found in 'game_values' directory.");
			OptionCheckbox("Output Data For Multiple Sessions", config::MultiOutputStreamTxt, "Outputs folders for multiple concurrent sessions. Separated into folders by consecutive session count.");
			if (config::MultiOutputStreamTxt)
			{
				int one = 1;
				ImGui::InputScalar("Max Sessions", ImGuiDataType_S32, &config::MultiOutputStreamTxtMax.get(), &one, NULL, "%d");
				ImGui::InputScalar("Current Index", ImGuiDataType_S32, &config::MultiOutputStreamTxtIdx.get(), &one, NULL, "%d");
			}
		}
	}
	header("Other");
	{
#ifdef __linux__
		OptionCheckbox("Linux: Copy Missing Shared EEPROM/NVMEM/VMU Files", config::CopyMissingSharedMem, "Copies game memory files from shared assets directory if missing from home directory.");
#endif
		if (config::UseReios)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}
		OptionCheckbox("Online & Replays: Force Real DC BIOS", config::ForceRealBios, "Forces real Dreamcast BIOS when made available. Disabled by default for online & games and replays.");
		if (config::UseReios)
		{
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}
		if (config::ForceRealBios)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}
		OptionCheckbox("HLE BIOS", config::UseReios, "Force high-level BIOS emulation");
		if (config::ForceRealBios)
		{
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}
		OptionCheckbox("Force Windows CE", config::ForceWindowsCE,
					   "Enable full MMU emulation and other Windows CE settings. Do not enable unless necessary");
		OptionCheckbox("Multi-threaded emulation", config::ThreadedRendering,
					   "Run the emulated CPU and GPU on different threads");
#ifndef __ANDROID
		OptionCheckbox("Serial Console", config::SerialConsole,
					   "Dump the Dreamcast serial console to stdout");
#endif
		OptionCheckbox("Dump Textures", config::DumpTextures,
					   "Dump all textures into data/texdump/<game id>");

		OptionCheckbox("Show Eject Disk", config::ShowEjectDisk,
					   "Show Eject Disk button in Menu");

		bool logToFile = cfgLoadBool("log", "LogToFile", false);
		bool newLogToFile = logToFile;
		ImGui::Checkbox("Log to File", &newLogToFile);
		if (logToFile != newLogToFile)
		{
			cfgSaveBool("log", "LogToFile", newLogToFile);
			LogManager::Shutdown();
			LogManager::Init();
		}
		ImGui::SameLine();
		ShowHelpMarker("Log debug information to flycast.log");
#ifdef SENTRY_UPLOAD
	            OptionCheckbox("Automatically Report Crashes", config::UploadCrashLogs,
	            		"Automatically upload crash reports to sentry.io to help in troubleshooting. No personal information is included.");
#endif
	}
	ImGui::PopStyleVar();

#ifdef USE_LUA
	header("Lua Scripting");
	{
		char LuaFileName[256];

		strcpy(LuaFileName, config::LuaFileName.get().c_str());
		ImGui::InputText("Lua Filename", LuaFileName, sizeof(LuaFileName), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
		ImGui::SameLine();
		ShowHelpMarker("Specify lua filename to use. Should be located in Flycast config directory. Defaults to flycast.lua when empty.");
		config::LuaFileName = LuaFileName;
	}
#endif
}

void GuiSettings::settings_body_about(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	header("Flycast Dojo");
	{
		ImGui::Text("Version: %s", GIT_VERSION);
		ImGui::Text("Git Hash: %s", GIT_HASH);
		ImGui::Text("Build Date: %s", BUILD_DATE);
	}
	ImGui::Spacing();
	header("Platform");
	{
		ImGui::Text("CPU: %s",
#if HOST_CPU == CPU_X86
					"x86"
#elif HOST_CPU == CPU_ARM
					"ARM"
#elif HOST_CPU == CPU_MIPS
					"MIPS"
#elif HOST_CPU == CPU_X64
					"x86/64"
#elif HOST_CPU == CPU_GENERIC
					"Generic"
#elif HOST_CPU == CPU_ARM64
					"ARM64"
#else
					"Unknown"
#endif
		);
		ImGui::Text("Operating System: %s",
#ifdef __ANDROID__
					"Android"
#elif defined(__unix__)
					"Linux"
#elif defined(__APPLE__)
#ifdef TARGET_IPHONE
					"iOS"
#else
					"macOS"
#endif
#elif defined(TARGET_UWP)
					"Windows Universal Platform"
#elif defined(_WIN32)
					"Windows"
#elif defined(__SWITCH__)
					"Switch"
#else
					"Unknown"
#endif
		);
#ifdef TARGET_IPHONE
		extern std::string iosJitStatus;
		ImGui::Text("JIT Status: %s", iosJitStatus.c_str());
#endif
	}
	ImGui::Spacing();
	if (isOpenGL(config::RendererType))
		header("OpenGL");
	else if (isVulkan(config::RendererType))
		header("Vulkan");
	else if (isDirectX(config::RendererType))
		header("DirectX");
	ImGui::Text("Driver Name: %s", GraphicsContext::Instance()->getDriverName().c_str());
	ImGui::Text("Version: %s", GraphicsContext::Instance()->getDriverVersion().c_str());
	ImGui::PopStyleVar();
}

void GuiSettings::settings_body_credits(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	{
		ImGui::Text("Flycast Dojo is maintained by blueminder");
		ImGui::Text("Based on the Flycast emulator maintained by flyinghead");
		std::string contributors_url = "https://github.com/blueminder/flycast-dojo/graphs/contributors";
#if !defined(__ANDROID__)
		if (ImGui::Button("GitHub Contributors"))
#ifdef _WIN32
			ShellExecute(0, 0, contributors_url.c_str(), 0, 0 , SW_SHOW );
#elif defined(__APPLE__)
			system(std::string("open " + contributors_url).c_str());
#elif defined(__linux__)
			system(std::string("xdg-open " + contributors_url).c_str());
#endif
#endif
	}
	ImGui::Spacing();
	header("Special Thanks");
	{
		ImGui::BulletText("flyinghead");
		ImGui::SameLine();
		ShowHelpMarker("For making Flycast possible. Thank you for your continued maintenance and guidance.");

		ImGui::BulletText("vkedwardli");
		ImGui::SameLine();
		ShowHelpMarker("For regular feedback and Mac OS optimizations.");

		ImGui::BulletText("pof & shine");
		ImGui::SameLine();
		ShowHelpMarker("For consistently giving good advice and integrating Flycast Dojo into Fightcade.");

		ImGui::BulletText("Ren");
		ImGui::SameLine();
		ShowHelpMarker("For extensive testing of Relays and integrating Flycast Dojo into Arkadyzja.");
	}
	ImGui::Spacing();
	header("Champion Patrons");
	{
		ImGui::BulletText("Arlieth");
		ImGui::BulletText("Gregory Kennedy");
		ImGui::BulletText("Heather Graves");
		ImGui::BulletText("Jasen Parayno");
		ImGui::BulletText("JellyfishKrag");
		ImGui::BulletText("Rob");
		ImGui::BulletText("styroteqe");
		ImGui::BulletText("Weffen");
		ImGui::BulletText("zmannnn");
		ImGui::Spacing();
		std::string patreon_url = "https://www.patreon.com/projectdojo";
#if !defined(__ANDROID__)
		if (ImGui::Button("Support on Patreon"))
#ifdef _WIN32
			ShellExecute(0, 0, patreon_url.c_str(), 0, 0 , SW_SHOW );
#elif defined(__APPLE__)
			system(std::string("open " + patreon_url).c_str());
#elif defined(__linux__)
			system(std::string("xdg-open " + patreon_url).c_str());
#endif
#endif
	}

	ImGui::PopStyleVar();
}

void GuiSettings::settings_body_update(ImVec2 normal_padding)
{
#ifdef _WIN32
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
	//header("Update");
	{
		static int channel_current_idx = 0;
		const char* channels[] = { "Stable", "Preview" };

		update_channel = config::UpdateChannel.get();
		if (update_channel == "stable")
			channel_current_idx = 0;
		else if (update_channel == "preview")
			channel_current_idx = 1;

		if(ImGui::Combo("Update Channel", &channel_current_idx, channels, IM_ARRAYSIZE(channels)))
		{
			latest = "";
			update_channel = channels[channel_current_idx];
			update_channel[0] = tolower(update_channel[0]);

			config::UpdateChannel = update_channel;
		}

		if (ImGui::Button("Check for Latest Version"))
		{
			dojo_file.tag_download = dojo_file.GetLatestDownloadUrl(update_channel);
			latest = std::get<0>(dojo_file.tag_download);
		}

		if (latest.size() > 0)
		{
			ImGui::Text("Latest %s Release: %s", channels[channel_current_idx], latest.c_str());

			char buffer[40] = { 0 };
			snprintf(buffer, 40, "%s", GIT_VERSION);
			if(strcmp(buffer, latest.c_str()) != 0)
			{
				if (ImGui::Button("Download"))
				{
					dojo_file.tag_download = dojo_file.GetLatestDownloadUrl(update_channel);
					ImGui::OpenPopup("Download?");
				}

				ImGui::SameLine();

				if (ImGui::Button("Download & Update"))
				{
					dojo_file.tag_download = dojo_file.GetLatestDownloadUrl(update_channel);
					ImGui::OpenPopup("Update?");
				}
			}
			else
			{
				ImGui::TextColored(ImVec4(0, 128, 0, 1), "You are already on the latest version.");
			}
		}

		dojo_gui.update_action();
	}

	header("Switch Version");
	{
		static int switch_current_idx = 0;

		if (dojo_file.versions.size() == 0)
		{
			dojo_file.versions = dojo_file.ListVersions();
		}

		if (dojo_file.versions.size() > 0)
		{
			if(ImGui::BeginCombo("Downloaded Versions", dojo_file.ExtractTag(dojo_file.versions.at(switch_current_idx)).c_str(), 0)) {
			    for (int i = 0; i < dojo_file.versions.size(); ++i) {
			        const bool isSelected = (switch_current_idx == i);
			        if (ImGui::Selectable(dojo_file.ExtractTag(dojo_file.versions[i]).c_str(), isSelected)) {
			            switch_current_idx = i;
			        }

			        if (isSelected) {
			            ImGui::SetItemDefaultFocus();
			        }
			    }
			    ImGui::EndCombo();
			}

			if (ImGui::Button("Switch Version##btn"))
			{
				dojo_file.switch_version = dojo_file.ExtractTag(dojo_file.versions.at(switch_current_idx));
				ImGui::OpenPopup("Switch?");
			}
		}
		else
		{
			ImGui::Text("No other Flycast Dojo versions found.\n\nYou can either:\n1. Download from an update channel above.\n2. Drop a release package into your program directory.");
			std::string releases_url = "https://github.com/blueminder/flycast-dojo/releases";
			if (ImGui::Button("GitHub Releases"))
				ShellExecute(0, 0, releases_url.c_str(), 0, 0 , SW_SHOW );

			ImGui::SameLine();

			wchar_t szPath[MAX_PATH];
			GetModuleFileNameW( NULL, szPath, MAX_PATH );
			std::string path = ghc::filesystem::path{ szPath }.parent_path().string();

			if (ImGui::Button("Open Program Directory"))
				ShellExecute(0, "open", path.c_str(), 0, 0 , SW_SHOW );
		}

		dojo_gui.switch_action();
	}
	ImGui::PopStyleVar();
#endif
}

void GuiSettings::settings_body_video(ImVec2 normal_padding)
{
	int renderApi;
	bool perPixel;
	switch (config::RendererType)
	{
	default:
	case RenderType::OpenGL:
		renderApi = 0;
		perPixel = false;
		break;
	case RenderType::OpenGL_OIT:
		renderApi = 0;
		perPixel = true;
		break;
	case RenderType::Vulkan:
		renderApi = 1;
		perPixel = false;
		break;
	case RenderType::Vulkan_OIT:
		renderApi = 1;
		perPixel = true;
		break;
	case RenderType::DirectX9:
		renderApi = 2;
		perPixel = false;
		break;
	case RenderType::DirectX11:
		renderApi = 3;
		perPixel = false;
		break;
	case RenderType::DirectX11_OIT:
		renderApi = 3;
		perPixel = true;
		break;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);

	ImGuiStyle &style = ImGui::GetStyle();
	float innerSpacing = style.ItemInnerSpacing.x;

	const bool has_per_pixel = GraphicsContext::Instance()->hasPerPixel();

	header("Basic Settings");
	{
		constexpr int apiCount = 0
#ifdef USE_VULKAN
								 + 1
#endif
#ifdef USE_DX9
								 + 1
#endif
#ifdef USE_OPENGL
								 + 1
#endif
#ifdef USE_DX11
								 + 1
#endif
			;

		std::vector<int> graphapi;
		std::vector<std::string> graphapiText;

		if (apiCount > 0)
		{
#ifdef USE_OPENGL
			graphapi.push_back(0);
			graphapiText.push_back("OpenGL");
#endif
#ifdef USE_VULKAN
			graphapi.push_back(1);
#ifdef __APPLE__
			graphapiText.push_back("Vulkan (Metal)");
#else
			graphapiText.push_back("Vulkan");
#endif
#endif
#ifdef USE_DX9
			graphapi.push_back(2);
			graphapiText.push_back("DirectX 9");
#endif
#ifdef USE_DX11
			graphapi.push_back(3);
			graphapiText.push_back("DirectX 11");
#endif
		}

		u32 gaSelected = 0;
		for (u32 i = 0; i < graphapi.size(); i++)
		{
			if (graphapi[i] == renderApi)
				gaSelected = i;
		}

		ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
		if (ImGui::BeginCombo("##Graphics API", graphapiText[gaSelected].c_str(), ImGuiComboFlags_None))
		{
			for (u32 i = 0; i < graphapi.size(); i++)
			{
				bool is_selected = graphapi[i] == renderApi;
				if (ImGui::Selectable(graphapiText[i].c_str(), is_selected))
					renderApi = graphapi[i];
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		ImGui::SameLine(0, innerSpacing);

		ImGui::Text("Graphics API");
		ImGui::SameLine();
		ShowHelpMarker("Chooses the backend to use for rendering. DirectX 9 offers the best compatibility on Windows, while OpenGL does for other platforms.");

		const std::array<float, 13> scalings{0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 4.5f, 5.f, 6.f, 7.f, 8.f, 9.f};
		const std::array<std::string, 13> scalingsText{"Half", "Native", "x1.5", "x2", "x2.5", "x3", "x4", "x4.5", "x5", "x6", "x7", "x8", "x9"};
		std::array<int, scalings.size()> vres;
		std::array<std::string, scalings.size()> resLabels;
		u32 selected = 0;
		for (u32 i = 0; i < scalings.size(); i++)
		{
			vres[i] = scalings[i] * 480;
			if (vres[i] == config::RenderResolution)
				selected = i;
			if (!config::Widescreen)
				resLabels[i] = std::to_string((int)(scalings[i] * 640)) + "x" + std::to_string((int)(scalings[i] * 480));
			else
				resLabels[i] = std::to_string((int)(scalings[i] * 480 * 16 / 9)) + "x" + std::to_string((int)(scalings[i] * 480));
			resLabels[i] += " (" + scalingsText[i] + ")";
		}

		ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
		if (ImGui::BeginCombo("##Resolution", resLabels[selected].c_str(), ImGuiComboFlags_None))
		{
			for (u32 i = 0; i < scalings.size(); i++)
			{
				bool is_selected = vres[i] == config::RenderResolution;
				if (ImGui::Selectable(resLabels[i].c_str(), is_selected))
					config::RenderResolution = vres[i];
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		ImGui::SameLine(0, innerSpacing);

		ImGui::Text("Internal Resolution");
		ImGui::SameLine();
		ShowHelpMarker("Internal render resolution. Higher is better, but more demanding on the GPU. Values higher than your display resolution (but no more than double your display resolution) can be used for supersampling, which provides high-quality antialiasing without reducing sharpness.");

		int renderer = perPixel ? 2 : config::PerStripSorting ? 1
															  : 0;

		std::vector<int> tsort{0, 1};
		std::vector<std::string> tsortText{"Per Triangle", "Per Strip"};

		if (has_per_pixel)
		{
			tsort.push_back(2);
			tsortText.push_back("Per Pixel");
		}

		u32 tsSelected = 0;
		for (u32 i = 0; i < tsort.size(); i++)
		{
			if (tsort[i] == renderer)
				tsSelected = i;
		}

		ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
		if (ImGui::BeginCombo("##Transparent Sorting", tsortText[tsSelected].c_str(), ImGuiComboFlags_None))
		{
			for (u32 i = 0; i < tsort.size(); i++)
			{
				bool is_selected = tsort[i] == renderer;
				if (ImGui::Selectable(tsortText[i].c_str(), is_selected))
					renderer = tsort[i];
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		ImGui::SameLine(0, innerSpacing);

		ImGui::Text("Transparent Sorting");
		ImGui::SameLine();

		std::string help_text;
		switch (renderer)
		{
		case 0:
			perPixel = false;
			config::PerStripSorting.set(false);
			help_text = "Per Triangle: Sort transparent polygons per triangle. Fast but may produce graphical glitches";
			break;
		case 1:
			perPixel = false;
			config::PerStripSorting.set(true);
			help_text = "Per Strip: Sort transparent polygons per strip. Faster but may produce graphical glitches";
			break;
		case 2:
			perPixel = true;
			help_text = "Per Pixel: Sort transparent polygons per pixel. Slower but accurate";
			break;
		}

		ShowHelpMarker(help_text.c_str());
	}

	if (perPixel)
	{
		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Per Pixel Settings", ImGuiTreeNodeFlags_None))
		{

			const std::array<int64_t, 4> bufSizes{(u64)512 * 1024 * 1024, (u64)1024 * 1024 * 1024, (u64)2 * 1024 * 1024 * 1024, (u64)4 * 1024 * 1024 * 1024};
			const std::array<std::string, 4> bufSizesText{"512 MB", "1 GB", "2 GB", "4 GB"};
			ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
			u32 selected = 0;
			for (; selected < bufSizes.size(); selected++)
				if (bufSizes[selected] == config::PixelBufferSize)
					break;
			if (selected == bufSizes.size())
				selected = 0;
			if (ImGui::BeginCombo("##PixelBuffer", bufSizesText[selected].c_str(), ImGuiComboFlags_NoArrowButton))
			{
				for (u32 i = 0; i < bufSizes.size(); i++)
				{
					bool is_selected = i == selected;
					if (ImGui::Selectable(bufSizesText[i].c_str(), is_selected))
						config::PixelBufferSize = bufSizes[i];
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
						selected = i;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine(0, innerSpacing);

			if (ImGui::ArrowButton("##Decrease BufSize", ImGuiDir_Left))
			{
				if (selected > 0)
					config::PixelBufferSize = bufSizes[selected - 1];
			}
			ImGui::SameLine(0, innerSpacing);
			if (ImGui::ArrowButton("##Increase BufSize", ImGuiDir_Right))
			{
				if (selected < bufSizes.size() - 1)
					config::PixelBufferSize = bufSizes[selected + 1];
			}
			ImGui::SameLine(0, style.ItemInnerSpacing.x);

			ImGui::Text("Pixel Buffer Size");
			ImGui::SameLine();
			ShowHelpMarker("The size of the pixel buffer. May need to be increased when upscaling by a large factor.");

			OptionSlider("Maximum Layers", config::PerPixelLayers, 8, 128,
						 "Maximum number of transparent layers. May need to be increased for some complex scenes. Decreasing it may improve performance.");
		}
	}

	header("On-Screen Display");
	{
		OptionCheckbox("Show FPS Counter", config::ShowFPS, "Show on-screen frame/sec counter");
		OptionCheckbox("Show VMU In-game", config::FloatVMUs, "Show the VMU LCD screens while in-game");
	}

	ImGui::Spacing();
	if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_None))
	{
		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Texture Filtering:");
			ImGui::Columns(2, "textureFiltering", false);
			OptionRadioButton("Default", config::TextureFiltering, 0, "Use the game's default texture filtering");
			ImGui::NextColumn();
			ImGui::NextColumn();
			OptionRadioButton("Force Nearest-Neighbor", config::TextureFiltering, 1, "Force nearest-neighbor filtering for all textures. Crisper appearance, but may cause various rendering issues. This option usually does not affect performance.");
			ImGui::NextColumn();
			OptionRadioButton("Force Linear", config::TextureFiltering, 2, "Force linear filtering for all textures. Smoother appearance, but may cause various rendering issues. This option usually does not affect performance.");
			ImGui::Columns(1, nullptr, false);

			const std::array<int, 5> aniso{ 1, 2, 4, 8, 16 };
			const std::array<std::string, 5> anisoText{ "Disabled", "2x", "4x", "8x", "16x" };
			u32 afSelected = 0;
			for (u32 i = 0; i < aniso.size(); i++)
			{
				if (aniso[i] == config::AnisotropicFiltering)
					afSelected = i;
			}

			ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
			if (ImGui::BeginCombo("##Anisotropic Filtering", anisoText[afSelected].c_str(), ImGuiComboFlags_NoArrowButton))
			{
				for (u32 i = 0; i < aniso.size(); i++)
				{
					bool is_selected = aniso[i] == config::AnisotropicFiltering;
					if (ImGui::Selectable(anisoText[i].c_str(), is_selected))
						config::AnisotropicFiltering = aniso[i];
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine(0, innerSpacing);

			if (ImGui::ArrowButton("##Decrease Anisotropic Filtering", ImGuiDir_Left))
			{
				if (afSelected > 0)
					config::AnisotropicFiltering = aniso[afSelected - 1];
			}
			ImGui::SameLine(0, innerSpacing);
			if (ImGui::ArrowButton("##Increase Anisotropic Filtering", ImGuiDir_Right))
			{
				if (afSelected < aniso.size() - 1)
					config::AnisotropicFiltering = aniso[afSelected + 1];
			}
			ImGui::SameLine(0, style.ItemInnerSpacing.x);

			ImGui::Text("Anisotropic Filtering");
			ImGui::SameLine();
			ShowHelpMarker("Higher values make textures viewed at oblique angles look sharper, but are more demanding on the GPU. This option only has a visible impact on mipmapped textures.");

			ImGui::Spacing();

			header("Texture Upscaling");
			{
#ifdef _OPENMP
				OptionArrowButtons("Texture Upscaling", config::TextureUpscale, 1, 8,
								   "Upscale textures with the xBRZ algorithm. Only on fast platforms and for certain 2D games");
				OptionSlider("Texture Max Size", config::MaxFilteredTextureSize, 8, 1024,
							 "Textures larger than this dimension squared will not be upscaled");
				OptionArrowButtons("Max Threads", config::MaxThreads, 1, 8,
								   "Maximum number of threads to use for texture upscaling. Recommended: number of physical cores minus one");
#endif
				OptionCheckbox("Load Custom Textures", config::CustomTextures,
							   "Load custom/high-res textures from data/textures/<game id>");
			}

			OptionCheckbox("Render to Texture Buffer", config::RenderToTextureBuffer,
						   "Copy rendered-to textures back to VRAM. Slower but accurate");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Fixed Frequency:");
			ImGui::SameLine();
			ShowHelpMarker("Set static frequency. Optimized for consistent input polling & frame rate. Recommended");
			ImGui::Columns(3, "fixed_freq", false);
			OptionRadioButton("Disabled##FF", config::FixedFrequency, 0, "Frame rate will be dependent on VSync or Audio Sync");
			ImGui::NextColumn();
			OptionRadioButton("Auto", config::FixedFrequency, 1, "Automatically sets frequency by Cable & Broadcast type");
			ImGui::NextColumn();
			OptionRadioButton("59.94 Hz", config::FixedFrequency, 2, "Native NTSC/VGA frequency");
			ImGui::NextColumn();
			OptionRadioButton("60 Hz", config::FixedFrequency, 3, "Approximate NTSC/VGA frequency");
			ImGui::NextColumn();
			OptionRadioButton("50 Hz", config::FixedFrequency, 4, "Native PAL frequency");
			ImGui::NextColumn();
			OptionRadioButton("30 Hz", config::FixedFrequency, 5, "Half NTSC/VGA frequency");
			ImGui::Columns(1, nullptr, false);

			if (config::FixedFrequency != 0)
			{
				OptionCheckbox("FF CPU Optimizations", config::FixedFrequencyThreadSleep, "Improves CPU Usage in exchange for less precise frame timing. Only enable on older PCs.");
			}

#ifndef TARGET_IPHONE
			OptionCheckbox("VSync", config::VSync, "Synchronizes the frame rate with the screen refresh rate.");
			if (isVulkan(config::RendererType))
			{
				ImGui::Indent();
				{
					DisabledScope scope(!config::VSync);
					OptionCheckbox("Duplicate frames", config::DupeFrames, "Duplicate frames on high refresh rate monitors (120 Hz and higher)");
				}
				ImGui::Unindent();
			}
#endif
			OptionCheckbox("Delay Frame Swapping", config::DelayFrameSwapping,
						   "Useful to avoid flashing screen or glitchy videos. Not recommended on slow platforms");
			OptionCheckbox("Fix Upscale Bleeding Edge", config::FixUpscaleBleedingEdge,
							"Helps with texture bleeding case when upscaling. Disabling it can help if pixels are warping when upscaling in 2D games (MVC2, CVS, KOF, etc.)");
			OptionCheckbox("Native Depth Interpolation", config::NativeDepthInterpolation,
						   "Helps with texture corruption and depth issues on AMD GPUs. Can also help Intel GPUs in some cases.");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_None))
		{
			OptionCheckbox("Shadows", config::ModifierVolumes,
						   "Enable modifier volumes, usually used for shadows");
			OptionCheckbox("Fog", config::Fog, "Enable fog effects");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Widescreen##Section", ImGuiTreeNodeFlags_None))
		{
			OptionCheckbox("Widescreen", config::Widescreen,
						   "Draw geometry outside of the normal 4:3 aspect ratio. May produce graphical glitches in the revealed areas.\nAspect Fit and shows the full 16:9 content.");
			{
				DisabledScope scope(!config::Widescreen);

				ImGui::Indent();
				{
					DisabledScope scope(!config::Widescreen);

					OptionCheckbox("Super Widescreen", config::SuperWidescreen,
								   "Use the full width of the screen or window when its aspect ratio is greater than 16:9.\nAspect Fill and remove black bars.");
					ImGui::Unindent();
				}
			}
			OptionCheckbox("Widescreen Game Cheats", config::WidescreenGameHacks,
						   "Modify the game so that it displays in 16:9 anamorphic format and use horizontal screen stretching. Only some games are supported.");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_None))
		{
			OptionSlider("Horizontal Stretching", config::ScreenStretching, 100, 250,
						 "Stretch the screen horizontally");

			OptionCheckbox("Rotate Screen 90°", config::Rotate90, "Rotate the screen 90° counterclockwise");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Frame Skipping", ImGuiTreeNodeFlags_None))
		{
			OptionArrowButtons("Frame Skipping", config::SkipFrame, 0, 6,
							   "Number of frames to skip between two actually rendered frames");

			ImGui::Text("Automatic Frame Skipping:");
			ImGui::Columns(3, "autoskip", false);
			OptionRadioButton("Disabled", config::AutoSkipFrame, 0, "No frame skipping");
			ImGui::NextColumn();
			OptionRadioButton("Normal", config::AutoSkipFrame, 1, "Skip a frame when the GPU and CPU are both running slow");
			ImGui::NextColumn();
			OptionRadioButton("Maximum", config::AutoSkipFrame, 2, "Skip a frame when the GPU is running slow");
			ImGui::Columns(1, nullptr, false);
		}
	}

	ImGui::PopStyleVar();

	switch (renderApi)
	{
	case 0:
		config::RendererType = perPixel ? RenderType::OpenGL_OIT : RenderType::OpenGL;
		break;
	case 1:
		config::RendererType = perPixel ? RenderType::Vulkan_OIT : RenderType::Vulkan;
		break;
	case 2:
		config::RendererType = RenderType::DirectX9;
		break;
	case 3:
		config::RendererType = perPixel ? RenderType::DirectX11_OIT : RenderType::DirectX11;
		break;
	}
}

#ifdef USE_GROOVY

// Local widgets for the Groovy options.
//
// gui_util.h's OptionComboBox/OptionSlider/OptionRadioButton take
// config::Option<int>, which is Option<int, PerGameOption=true>. Every Groovy
// option is deliberately Option<int, false>, so they do not bind - and that is
// not a naming detail worth "fixing" by flipping the flag:
//
//   Option::load() (core/cfg/option.h:122) takes the per-game branch when
//   PerGameOption is true AND a per-game config exists, and that branch never
//   checks cfgIsVirtual(). Since cfgIsVirtual() is what makes a command-line
//   -config value stick, a per-game option set from the command line is
//   silently ignored for any game that has per-game settings. Fightcade drives
//   this whole feature through -config, so these must stay non-per-game.
//
// Hence these three small helpers. They mirror the gui_util versions exactly.
static void groovyComboBox(const char *name, config::Option<int, false>& option,
		const char *values[], int count, const char *help = nullptr)
{
	int index;
	for (index = 0; index < count; index++)
		if (option == index)
			break;
	if (index == count)
		index = 0;

	DisabledScope scope(option.isReadOnly());
	if (ImGui::BeginCombo(name, values[index], ImGuiComboFlags_None))
	{
		for (int i = 0; i < count; i++)
		{
			const bool selected = index == i;
			if (ImGui::Selectable(values[i], selected) && !option.isReadOnly())
				option.set(i);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (help != nullptr)
	{
		ImGui::SameLine();
		ShowHelpMarker(help);
	}
}

static void groovySlider(const char *name, config::Option<int, false>& option,
		int min, int max, const char *fmt, const char *help = nullptr)
{
	DisabledScope scope(option.isReadOnly());
	int value = option;
	if (ImGui::SliderInt(name, &value, min, max, fmt) && !option.isReadOnly())
		option.set(value);
	if (help != nullptr)
	{
		ImGui::SameLine();
		ShowHelpMarker(help);
	}
}

// There is no OptionString text-input helper in gui_util, so string options are
// raw InputText + set(), the same way the Dojo UI already does it elsewhere.
static void groovyTextInput(const char *name, config::Option<std::string, false>& option,
		const char *help = nullptr)
{
	DisabledScope scope(option.isReadOnly());
	char buf[256];
	strncpy(buf, option.get().c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if (ImGui::InputText(name, buf, sizeof(buf), ImGuiInputTextFlags_CharsNoBlank) && !option.isReadOnly())
		option.set(std::string(buf));
	if (help != nullptr)
	{
		ImGui::SameLine();
		ShowHelpMarker(help);
	}
}

void GuiSettings::settings_body_mister(ImVec2 normal_padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);

	OptionCheckbox("Enable Groovy MiSTer NLC", config::GroovyEnable,
			"Stream frames to a Groovy MiSTer (DE10-Nano) over the network for display on a CRT. "
			"The PC window keeps running as normal.");

	if (!config::GroovyEnable)
	{
		ImGui::Spacing();
		ImGui::TextWrapped("Requires a MiSTer running the Groovy core, reachable over the network. "
				"A direct GbE cable with static IPs on both ends is strongly recommended - no switch, no Wi-Fi.");
		ImGui::PopStyleVar();
		return;
	}

	ImGui::Spacing();
	header("Connection");
	groovyTextInput("MiSTer IP", config::GroovyHost, "IP address of the MiSTer. Default 192.168.100.2.");
	// Port is deliberately not exposed. It is fixed at 32100 by the Groovy core
	// and we have no handling for it changing mid-session (it rides CMD_INIT, so
	// a change needs a full reconnect). The config option still exists and is
	// still honoured, so -config groovy:Port=N works if it is ever needed.

	ImGui::Spacing();
	header("Monitor");
	{
		// The allowlist is ours because switchres cannot be asked whether a
		// preset name was valid - it stores and echoes back whatever it is
		// given, keeps the previous ranges, and logs one line nobody reads.
		int presetCount = 0;
		const char * const *presets = groovy::monitorPresetList(presetCount);
		const std::string& current = config::GroovyMonitorPreset.get();
		DisabledScope scope(config::GroovyMonitorPreset.isReadOnly());
		if (ImGui::BeginCombo("Monitor Preset", current.c_str(), ImGuiComboFlags_None))
		{
			for (int i = 0; i < presetCount; i++)
			{
				const bool selected = current == presets[i];
				if (ImGui::Selectable(presets[i], selected))
					config::GroovyMonitorPreset.set(std::string(presets[i]));
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}
	ImGui::SameLine();
	ShowHelpMarker("CRT type. arcade_15 is a 15kHz arcade monitor; arcade_15_25_31 is tri-sync and "
			"also gives real 480p on VGA-mode games. A 15kHz-only monitor cannot show 480p, so "
			"480-line modes are sent as an interlaced signal instead.");
	groovyTextInput("switchres.ini", config::GroovySwitchresIni,
			"Optional switchres ini for a custom monitor. Leave blank for the preset above.");

	ImGui::Spacing();
	header("Stream");
	{
		static const char *codecs[] = { "Raw", "LZ4", "LZ4 delta", "LZ4HC", "LZ4HC delta",
				"LZ4 adaptive", "LZ4 adaptive delta", "NLC" };
		groovyComboBox("Codec", config::GroovyCodec, codecs, 8,
				"NLC is the default and gives the best quality per byte. LZ4 is the fallback for "
				"an older core that cannot do NLC, and is cheaper to encode. Raw cannot sustain "
				"480p - it is over the bandwidth ceiling.");
	}
	if (config::GroovyCodec == 7)
	{
		// Only two distinct packs, but the wire values are 0/1 = TILED and
		// 2 = RICE, so this cannot be a straight index-to-value combo like the
		// others - hence the explicit mapping rather than groovyComboBox().
		{
			static const char *packs[] = { "Tiled", "Rice" };
			const int index = config::GroovyNlcPack >= 2 ? 1 : 0;
			DisabledScope scope(config::GroovyNlcPack.isReadOnly());
			if (ImGui::BeginCombo("NLC Pack", packs[index], ImGuiComboFlags_None))
			{
				for (int i = 0; i < 2; i++)
					if (ImGui::Selectable(packs[i], index == i))
						config::GroovyNlcPack.set(i == 1 ? 2 : 1);
				ImGui::EndCombo();
			}
		}
		ImGui::SameLine();
		ShowHelpMarker("Rice needs a rice-capable core. An older core misparses Rice as Tiled and "
				"shows garbage with no error, so drop to Tiled if the picture is scrambled.");
		groovySlider("NLC Level", config::GroovyNearLevel, 0, 3, "%d",
				"0 is lossless but can peak over the bandwidth ceiling. 1 is the default and is "
				"visually indistinguishable from lossless on a CRT.");
	}
	{
		// NLC is RGB888 only: the FPGA decoder is three planes at three bytes per
		// pixel, so RGBA888 desyncs the stream and RGB565 the encoder refuses. The
		// client rejects both at CmdInit. Coerce rather than just hiding them, so a
		// config that already has one saved does not sit in an unreachable state.
		const bool nlc = config::GroovyCodec == 7;
		if (nlc && config::GroovyRgbMode != 0)
			config::GroovyRgbMode.set(0);

		static const char *rgb[] = { "RGB888", "RGBA888", "RGB565" };
		groovyComboBox("Colour Depth", config::GroovyRgbMode, rgb, nlc ? 1 : 3,
				nlc ? "NLC is RGB888 only - the FPGA decoder cannot carry an alpha channel or "
						"565 pixels. Switch to LZ4 if you need RGB565 for bandwidth."
					: "RGB565 halves bandwidth at the cost of banding.");
	}
	{
		static const char *mtus[] = { "1500", "3800 (jumbo frames)" };
		int index = config::GroovyMtu >= 3800 ? 1 : 0;
		DisabledScope scope(config::GroovyMtu.isReadOnly());
		if (ImGui::BeginCombo("MTU", mtus[index], ImGuiComboFlags_None))
		{
			for (int i = 0; i < 2; i++)
				if (ImGui::Selectable(mtus[i], index == i))
					config::GroovyMtu.set(i == 1 ? 3800 : 1500);
			ImGui::EndCombo();
		}
	}
	ImGui::SameLine();
	ShowHelpMarker("3800 requires 'Server -> Jumbo frames = On' in the Groovy core's OSD.");

	ImGui::Spacing();
	header("Audio");
	{
		static const char *audio[] = { "Off", "PC and MiSTer", "MiSTer only" };
		groovyComboBox("Audio Output", config::GroovyAudioMode, audio, 3,
				"'MiSTer only' is the default - the cabinet's speakers are the point, and the same "
				"audio from the PC half a frame out of step is worse than one source. Pick "
				"'PC and MiSTer' to keep both. Requires 'Audio = On' in the Groovy core's OSD; if "
				"that is off the core drops audio, the status below says so, and the PC keeps its "
				"sound. Changing this reconnects, because the audio rate is fixed when the session "
				"opens.");
	}

	ImGui::Spacing();
	header("Latency");
	groovySlider("Frame Delay Line", config::GroovyVCountSync, 0, 525, "%d",
			"Raster line each frame syncs to. 0 is automatic. Note that automatic only works when "
			"the CRT owns the frame clock, which is offline play - during netplay flycast's own "
			"clock stays in charge and line 1 is used instead.");
	groovySlider("Frame Delay Margin", config::GroovyFdMarginNs, 0, 3000000, "%d ns",
			"Safety headroom for the automatic frame delay calculation.");

	ImGui::Spacing();
	header("MiSTer Inputs");
	OptionCheckbox("Use MiSTer Controllers", config::GroovyUseInputs,
			"Use the pads plugged into the MiSTer instead of (or alongside) controllers on this PC, "
			"so a cabinet needs no host-side controller. Note this adds a network hop compared with "
			"a stick plugged into the PC, so it is a convenience rather than a latency win. "
			"Requires 'Server > Send inputs > Joysticks' in the Groovy core's OSD.");
	if (config::GroovyUseInputs)
	{
		// Input port not exposed, same reasoning as the video port above: fixed
		// at 32101 by the core, bound before CmdInit, and still settable via
		// -config groovy:InputPort=N.
		OptionCheckbox("Rumble", config::GroovyRumble,
				"Requires a core reporting version 2 or newer. The MiSTer gates this further: "
				"MiSTer.ini RUMBLE, then System > Controllers > <player> > Rumble. If nothing "
				"vibrates, check those in that order.");
	}

	ImGui::Spacing();
	header("Safety and Diagnostics");
	OptionCheckbox("CRT Safety Limit", config::GroovyCrtSafetyCap,
			"Refuse modelines outside a conservative envelope for an arcade CRT. Only turn this "
			"off if you know your monitor can take it - switchres will happily compute a mode "
			"that your CRT cannot.");
	OptionCheckbox("Auto Reconnect", config::GroovyAutoReconnect,
			"Reconnect automatically if the MiSTer stops acknowledging frames.");
	{
		static const char *levels[] = { "Errors only", "Errors and telemetry", "Full trace" };
		groovyComboBox("Log Level", config::GroovyLogLevel, levels, 3);
	}
	OptionCheckbox("Write Log File", config::GroovyLogToFile,
			"Write a Groovy log next to your data files. Independent of flycast's own logging, "
			"which is off by default and has nowhere to go in a windowed build.");

	ImGui::Spacing();
	header("Status");
	{
		groovy::OutputStatus status;
		groovy::getStatus(status);

		ImGui::Text("State: %s", status.state.c_str());
		if (status.connected)
		{
			if (status.haveModeline)
			{
				const groovy::Modeline& m = status.modeline;
				ImGui::Text("Source: %dx%d @%.3f Hz%s", status.srcWidth, status.srcHeight,
						status.srcRefresh, status.srcInterlaced ? " interlaced" : "");
				ImGui::Text("Modeline: %dx%d %.4f MHz, %s", m.hActive, m.vActive, m.pclock,
						m.interlace == groovy::INTERLACE_PROGRESSIVE ? "progressive"
								: "progressive framebuffer over interlaced");
			}
			ImGui::Text("Frames sent: %u   Last blit: %u bytes", status.framesSent, status.lastBlitBytes);
			if (status.reconnects > 0)
				ImGui::Text("Reconnects: %u", status.reconnects);

			// The only stream-health bit worth showing - vramSynced reads 1 through
			// real underruns, so it is not surfaced. See OutputStatus.
			if (status.frameskip)
				ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "MiSTer is skipping frames");

			ImGui::Text("Frame cost: %.2f ms pack+blit, %.2f ms sync overhead (worst %.2f ms over 5s)",
					status.packBlitMs, status.syncOverheadMs, status.worstFrameMs);
		}
	}

	ImGui::Spacing();
	if (ImGui::Button("Run Pixel Self-Test"))
	{
		const int rc = groovy::selfTest();
		if (rc == 0)
			gui_display_notification("MiSTer: pixel self-test passed", 3000);
		else
			gui_display_notification(("MiSTer: pixel self-test FAILED - " +
					std::string(groovy::selfTestResultName(rc))).c_str(), 6000);
	}
	ImGui::SameLine();
	ShowHelpMarker("Checks the wire pixel packing against known red/green/blue pixels. Catches a "
			"red/blue channel swap, which is the most common first-run problem.");

	ImGui::SameLine();
	if (ImGui::Button("Dump 4 Frames"))
		groovy::requestFrameDump(4, "");
	ImGui::SameLine();
	ShowHelpMarker("Writes the next 4 captured frames next to your data files as .ppm and .raw, "
			"so you can confirm the image is the right way up and the right way round without a "
			"MiSTer attached.");

	ImGui::Spacing();
	header("Log");
	{
		// The ring buffer has always been populated; it was simply never shown,
		// which is half of why the first hardware test produced no diagnostics.
		if (config::GroovyLogToFile)
			ImGui::TextWrapped("File: %s", groovy::logFileLocation().c_str());

		const std::deque<std::string>& lines = groovy::logRing();
		ImGui::BeginChild("groovy_log", ScaledVec2(0, 160), true,
				ImGuiWindowFlags_HorizontalScrollbar);
		if (lines.empty())
			ImGui::TextDisabled("(nothing logged yet)");
		else
			for (const std::string& line : lines)
				ImGui::TextUnformatted(line.c_str());
		// Follow the tail only while already at the bottom, so scrolling back to
		// read something is not yanked away by the next line.
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
	}

	ImGui::PopStyleVar();
}

#endif // USE_GROOVY
