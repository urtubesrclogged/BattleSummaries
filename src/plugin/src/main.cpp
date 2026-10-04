#include "Papyrus.h"
#include "Settings.h"
#include "SkyrimNet.h"
#include "Tracker.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	void InitLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) return;
		*path /= "BattleSummaries.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		spdlog::set_default_logger(std::move(log));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			BSM::Settings::Load();
			BSM::Tracker::Install();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			BSM::Tracker::Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			BSM::Tracker::Reset();
			BSM::SkyrimNet::Register();
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = { BSM_VERSION_MAJOR, BSM_VERSION_MINOR, BSM_VERSION_PATCH, 0 },  // from project(VERSION) in CMakeLists.txt
	.Name = "BattleSummaries",
	.Author = "urtubesrclogged",
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitLog();
	SKSE::log::info("Battle Summaries {}.{}.{} loading (runtime {})", BSM_VERSION_MAJOR, BSM_VERSION_MINOR, BSM_VERSION_PATCH, a_skse->RuntimeVersion().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	SKSE::GetPapyrusInterface()->Register(BSM::RegisterPapyrus);
	return true;
}
