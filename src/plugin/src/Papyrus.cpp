#include "Papyrus.h"

#include "Perf.h"
#include "Settings.h"
#include "Tracker.h"

namespace BSM
{
	namespace
	{
		constexpr auto kClass = "BattleSummaries_Native";
		using Tag = RE::StaticFunctionTag;

		std::string GetSummaryJson(Tag*, RE::Actor* a_actor) { return Tracker::SummaryJson(a_actor); }
		std::string GetSummaryText(Tag*, RE::Actor* a_actor) { return Tracker::SummaryPlain(a_actor); }
		std::string GetStatus(Tag*) { return Tracker::Status(); }
		std::string GetPerformance(Tag*) { return Perf::Report(); }
		void        ResetPerformance(Tag*) { Perf::Reset(); }
		std::string GetLastMemory(Tag*) { return Tracker::LastMemory(); }
		void        EndBattleNow(Tag*) { Tracker::EndNow(); }
		void        ReloadSettings(Tag*) { SKSE::GetTaskInterface()->AddTask([] { Settings::Load(); }); }  // on the main thread, which reads them
		void        SetVerboseLog(Tag*, bool a_on) { Settings::SetVerbose(a_on); }
		std::string DebugFakeRescue(Tag*, RE::Actor* a_victim) { return Tracker::FakeRescue(a_victim); }
		void ReportRegistered(Tag*, std::int32_t a_result)
		{
			if (a_result == 0) SKSE::log::info("SkyrimNet: the battle_summary decorator is registered");
			else SKSE::log::error("SkyrimNet: REFUSED to register the battle_summary decorator (result {}): NPCs will not be given summaries", a_result);
		}
		void ReportRemembered(Tag*, std::int32_t a_result)
		{
			if (a_result == 0) SKSE::log::info("SkyrimNet: the battle was accepted as a remembered event");
			else SKSE::log::error("SkyrimNet: REFUSED the remembered event (result {})", a_result);
		}
		std::string GetVersion(Tag*) { return std::format("{}.{}.{}", BSM_VERSION_MAJOR, BSM_VERSION_MINOR, BSM_VERSION_PATCH); }
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
#define REG(fn) a_vm->RegisterFunction(#fn, kClass, fn)
		REG(GetSummaryJson);
		REG(GetSummaryText);
		REG(GetStatus);
		REG(GetPerformance);
		REG(ResetPerformance);
		REG(GetLastMemory);
		REG(EndBattleNow);
		REG(ReloadSettings);
		REG(SetVerboseLog);
		REG(DebugFakeRescue);
		REG(ReportRegistered);
		REG(ReportRemembered);
		REG(GetVersion);
#undef REG
		return true;
	}
}
