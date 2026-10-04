#include "SkyrimNet.h"

#include <Windows.h>

namespace BSM::SkyrimNet
{
	namespace
	{
		constexpr auto kScript = "BattleSummaries_SkyrimNet";

		void Call(const char* a_fn, RE::BSScript::IFunctionArguments* a_args)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				delete a_args;
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
			vm->DispatchStaticCall(kScript, a_fn, a_args, cb);  // the VM owns the arguments from here
		}
	}

	bool Available() { return ::GetModuleHandleW(L"SkyrimNet.dll") != nullptr; }

	void Register()
	{
		if (!Available()) {
			SKSE::log::info("SkyrimNet is not loaded: battles are tracked, but nothing is told to anyone");
			return;
		}
		Call("Register", RE::MakeFunctionArguments());
		SKSE::log::info("SkyrimNet: asked for the battle_summary decorator to be registered");
	}

	void Remember(const std::string& a_text)
	{
		if (a_text.empty() || !Available()) return;
		SKSE::log::info("SkyrimNet: remembering \"{}\"", a_text);
		Call("Remember", RE::MakeFunctionArguments(RE::BSFixedString(a_text)));
	}
}
