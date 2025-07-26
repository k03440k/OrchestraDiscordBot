#include <dpp/dpp.h>
#include <GuelderConsoleLog.hpp>
#include <GuelderResourcesManager.hpp>

//#include "Utils.hpp"
#include "DiscordBot/OrchestraDiscordBot.hpp"

#define NOMINMAX

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace GuelderConsoleLog;
using namespace Orchestra;

void BotLogger(const dpp::log_t& log)
{
    switch(log.severity)
    {
    case dpp::ll_debug:
    case dpp::ll_info:
        GE_LOG(DPP, Info, log.message);
        break;
    case dpp::ll_warning:
        GE_LOG(DPP, Warning, log.message);
        break;
    case dpp::ll_error:
    case dpp::ll_critical:
        GE_LOG(DPP, Error, log.message);
        break;
    default:
        break;
    }
}

int main(int argc, char** argv)
{
    try
    {//issues with wchar_t's, admin ID
        std::filesystem::path path{ argv[0] };
        path.remove_filename();
        const GuelderResourcesManager::ResourcesManager resourcesManager{ path };
        const GuelderResourcesManager::ConfigFile cfg{ resourcesManager.GetFullPathToRelativeFile("Resources/main.cfg") };

        //TODO: it is better to make GetValue<std::string> and GetValue<const std::string&> in ResourcesManager
        auto botToken = cfg.GetVariable("botToken").GetValue<std::string>();
        auto commandPrefix = cfg.GetVariable("commandPrefix").GetValue<std::string>();
        auto yt_dlpPath = cfg.GetVariable("yt_dlp").GetValue<std::string>();
        const auto sentPacketsSize = cfg.GetVariable("sentPacketsSize").GetValue<unsigned int>();
        const auto logSentPackets = cfg.GetVariable("enableLoggingSentPackets").GetValue<bool>();
        const char paramPrefix = cfg.GetVariable("paramPrefix").GetRawValue()[0];
        unsigned long long adminSnowflake = 0;
        try
        {
            adminSnowflake = cfg.GetVariable("adminSnowflake").GetValue<unsigned long long>();
        }
        catch(...)
        {
            LogWarning("The variable \"adminSnowflake\" is empty, setting adminSnowflake to 0.");
        }

        OrchestraDiscordBot bot{ botToken, yt_dlpPath, commandPrefix, paramPrefix };

        bot.SetEnableLogSentPackets(logSentPackets);
        bot.SetSentPacketSize(sentPacketsSize);
        bot.SetAdminSnowflake(adminSnowflake);

        bot.SetLogger(BotLogger);

        bot.RegisterCommands();

        bot.Run();
    }
    catch(const OrchestraException& oe)
    {
        LogError("Caught an OrchestraException: ", oe.GetFullMessage());
    }
    catch(const std::exception& e)
    {
        LogError(e.what());
    }

#ifndef _DEBUG
    system("pause");
#endif

    return 0;
}