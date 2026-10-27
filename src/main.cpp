#include "Capture.h"
#include "MenuIntegration.h"
#include "Overlay.h"
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "Translation.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    void InitializeLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
        }
        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        *path /= std::format("{}.log", plugin->GetName());
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v"s);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        if (a_message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }
        // Without the framework nothing could draw the captured text, so
        // the Vanilla HUD keeps the notifications.
        if (SKSEMenuFramework::GetMenuFrameworkVersion() <= 0.0F) {
            logs::warn("SKSE Menu Framework is not installed; Vanilla notifications stay untouched");
            return;
        }
        NT::LoadSettings();
        // Data/Interface/Translations/NotificationTweaks_<LANGUAGE>.txt
        // (English defaults are compiled in; German ships alongside).
        NT::Translation::Import("NotificationTweaks");
        NT::Translation::LoadStrings();
        NT::Capture::Install();
        NT::Overlay::Initialize();
        NT::MenuIntegration::Initialize();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    InitializeLog();
    SKSE::Init(a_skse, false);

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    logs::info("{} v{} loaded (runtime {})", plugin->GetName(), plugin->GetVersion().string(), REL::Module::get().version().string());

    if (a_skse->IsEditor()) {
        logs::info("Creation Kit detected; plugin disabled");
        return false;
    }

    SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
    return true;
}
