#pragma once

// What the mod costs, measured where it runs: how often each piece of its work happens and how long it takes.
// Always on: a measurement is two clock reads and three atomic updates (tens of nanoseconds).

namespace BSM::Perf
{
	struct Stat
	{
		std::atomic<std::uint64_t> count{ 0 };
		std::atomic<std::uint64_t> totalNs{ 0 };
		std::atomic<std::uint64_t> maxNs{ 0 };

		void Add(std::uint64_t a_ns)
		{
			count.fetch_add(1, std::memory_order_relaxed);
			totalNs.fetch_add(a_ns, std::memory_order_relaxed);
			auto prev = maxNs.load(std::memory_order_relaxed);
			while (a_ns > prev && !maxNs.compare_exchange_weak(prev, a_ns, std::memory_order_relaxed)) {}
		}
		void Reset()
		{
			count = 0;
			totalNs = 0;
			maxNs = 0;
		}
	};

	// Times its own lifetime into the Stat it is pointed at; Into() may redirect it before it ends.
	class Scope
	{
	public:
		explicit Scope(Stat& a_stat) :
			stat(&a_stat), start(std::chrono::steady_clock::now()) {}
		~Scope() { stat->Add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count())); }
		void Into(Stat& a_stat) { stat = &a_stat; }

	private:
		Stat*                                 stat;
		std::chrono::steady_clock::time_point start;
	};

	inline Stat tickIdle;    // the quarter-second tick with no battle on
	inline Stat tickBattle;  // the tick during a battle
	inline Stat damageHook;  // our share of each health-damage call (the original's time is not counted)
	inline Stat eventSink;   // each combat / death / bleedout / magic-effect event the game reports
	inline Stat summary;     // building one NPC's summary for a prompt
	inline std::atomic<std::uint64_t> queueHigh{ 0 };     // most reports waiting at one tick
	inline std::atomic<std::uint64_t> queueDropped{ 0 };  // reports discarded because the queue was full
	inline std::atomic<std::uint64_t> mostParticipants{ 0 };
	inline std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();

	[[nodiscard]] std::string Report();
	void                      Reset();
}
