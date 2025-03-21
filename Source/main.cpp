#include <dpp/dpp.h>
#include <GuelderConsoleLog.hpp>
#include <GuelderResourcesManager.hpp>

//#include "Utils.hpp"
#include "DiscordBot/OrchestraDiscordBot.hpp"

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
    {
        const GuelderResourcesManager::ResourcesManager resourcesManager{ argv[0], "Resources", "config.txt" };

        const auto& botToken = resourcesManager.GetVariable("botToken").GetValue<std::string_view>();
        const auto& prefix = resourcesManager.GetVariable("commandPrefix").GetValue<std::string_view>();
        const auto& yt_dlpPath = resourcesManager.GetVariable("yt_dlp").GetValue<std::string_view>();
        const auto sentPacketsSize = resourcesManager.GetVariable("sentPacketsSize").GetValue<unsigned int>();
        const auto logSentPackets = resourcesManager.GetVariable("enableLoggingSentPackets").GetValue<bool>();
        const auto lazyPacketSend = resourcesManager.GetVariable("enableLazyPacketsSending").GetValue<bool>();
        const auto adminSnowflake = resourcesManager.GetVariable("adminSnowflake").GetValue<unsigned long long>();
        const char paramPrefix = resourcesManager.GetVariable("paramPrefix").GetValue<std::string_view>()[0];

        OrchestraDiscordBot bot{ botToken, yt_dlpPath.data(), prefix, paramPrefix };

        bot.SetEnableLogSentPackets(logSentPackets);
        bot.SetEnableLazyDecoding(lazyPacketSend);
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