#include "PCH.h"

#include "Keywords/Sanitizer.h"
#include "Settings/Settings.h"

SKSE_EXPORT constinit SKSE::PluginVersionData SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName("NullKeywordSantizer");
	v.AuthorName("Raziell74"sv);
	v.PluginVersion({ 0, 1, 0, 0 });
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({
		SKSE::RUNTIME_SSE_1_5_97,
		SKSE::RUNTIME_SSE_1_6_1170,
		SKSE::RUNTIME_SSE_1_7_99,
	});
	return v;
}();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->name = SKSEPlugin_Version.GetPluginName().data();
	pluginInfo->version = SKSEPlugin_Version.GetPluginVersion().pack();
	return true;
}

namespace
{
	void QueueScan(std::string_view a_reason)
	{
		const auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			SKSE::log::warn("Keyword scan ({}) skipped; task interface is not ready", a_reason);
			return;
		}

		tasks->AddTask([a_reason] {
			Keywords::ScanItems(a_reason);
		});
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message || !Settings::Get().enabled) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			QueueScan("kDataLoaded"sv);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			if (!a_message->data || *static_cast<const bool*>(a_message->data)) {
				QueueScan("kPostLoadGame"sv);
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			QueueScan("kNewGame"sv);
			break;
		default:
			break;
		}
	}

	class KeywordDistributionSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const SKSE::ModCallbackEvent*           a_event,
			RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (Settings::Get().enabled && a_event &&
				static_cast<std::string_view>(a_event->eventName) == "KID_KeywordDistributionDone"sv) {
				Keywords::ScanItems("KID_KeywordDistributionDone"sv);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	KeywordDistributionSink g_keywordDistributionSink{};
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// Debug CRT: Module::_instance is not constinit. If any Relocation resolved
	// during static init, get() marks the singleton initialized and then the
	// constructor zeros _base — later ID lookups jump to a raw offset (crash).
	REL::Module::reset();
	SKSE::Init(a_skse);
	Settings::Load();

	if (Settings::Get().enabled) {
		Keywords::InstallCopyHook();

		if (const auto* messaging = SKSE::GetMessagingInterface()) {
			if (!messaging->RegisterListener(OnSKSEMessage)) {
				SKSE::log::error("Failed to register SKSE message listener");
			}
		} else {
			SKSE::log::error("SKSE messaging interface is unavailable");
		}

		if (auto* events = SKSE::GetModCallbackEventSource()) {
			events->AddEventSink(&g_keywordDistributionSink);
		} else {
			SKSE::log::error("SKSE mod callback source is unavailable");
		}
	}

	const auto* plugin = SKSE::PluginVersionData::GetSingleton();
	SKSE::log::info("{} v{} loaded", plugin->GetPluginName(), plugin->GetPluginVersion().string());

	return true;
}
