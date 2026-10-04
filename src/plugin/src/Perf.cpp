#include "Perf.h"

namespace BSM::Perf
{
	namespace
	{
		std::string Line(const char* a_name, const Stat& a_s, double a_seconds)
		{
			const auto n = a_s.count.load();
			const auto total = a_s.totalNs.load();
			const double avgUs = n ? static_cast<double>(total) / static_cast<double>(n) / 1000.0 : 0.0;
			const double maxUs = static_cast<double>(a_s.maxNs.load()) / 1000.0;
			const double perSec = a_seconds > 0.0 ? static_cast<double>(n) / a_seconds : 0.0;
			// the share of one core this took, over the whole measured time
			const double share = a_seconds > 0.0 ? static_cast<double>(total) / 1e9 / a_seconds * 100.0 : 0.0;
			return std::format("{}: {} calls ({:.1f}/s), average {:.2f} us, worst {:.1f} us, total {:.2f} ms ({:.4f}% of one core)\n", a_name, n, perSec, avgUs, maxUs,
				static_cast<double>(total) / 1e6, share);
		}
	}

	std::string Report()
	{
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
		std::string  s = std::format("measured over {:.0f} s\n", seconds);
		s += Line("tick, no battle", tickIdle, seconds);
		s += Line("tick, in battle", tickBattle, seconds);
		s += Line("damage hook", damageHook, seconds);
		s += Line("game events", eventSink, seconds);
		s += Line("summary for a prompt", summary, seconds);
		s += std::format("most reports waiting at one tick: {}; dropped: {}; most participants in a battle: {}\n", queueHigh.load(), queueDropped.load(), mostParticipants.load());
		const auto all = tickIdle.totalNs + tickBattle.totalNs + damageHook.totalNs + eventSink.totalNs + summary.totalNs;
		s += std::format("all of it: {:.2f} ms, {:.4f}% of one core", static_cast<double>(all) / 1e6, seconds > 0.0 ? static_cast<double>(all) / 1e9 / seconds * 100.0 : 0.0);
		return s;
	}

	void Reset()
	{
		tickIdle.Reset();
		tickBattle.Reset();
		damageHook.Reset();
		eventSink.Reset();
		summary.Reset();
		queueHigh = 0;
		queueDropped = 0;
		mostParticipants = 0;
		since = std::chrono::steady_clock::now();
	}
}
