#include <GuelderResourcesManager.hpp>
#include <dpp/dpp.h>
#include <GuelderConsoleLog.hpp>

#include "Utils.hpp"
#include "DiscordBot/OrchestraDiscordBot.hpp"

using namespace GuelderConsoleLog;
using namespace GuelderResourcesManager;
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
        const ResourcesManager resourcesManager{ argv[0] };

        const auto& botToken = resourcesManager.GetResourcesVariableContent("botToken");
        const auto& prefix = resourcesManager.GetResourcesVariableContent("commandPrefix");
        const auto& yt_dlpPath = resourcesManager.GetResourcesVariableContent("yt_dlp");
        const auto sentPacketsSize = StringToInt(resourcesManager.GetResourcesVariableContent("sentPacketsSize"));
        const auto logSentPackets = StringToBool(resourcesManager.GetResourcesVariableContent("enableLoggingSentPackets"));
        const auto lazyPacketSend = StringToBool(resourcesManager.GetResourcesVariableContent("enableLazyPacketsSending"));

        LogInfo("Found token: ", botToken);

        OrchestraDiscordBot bot{ botToken, prefix, yt_dlpPath.data() };

        bot.SetEnableLogSentPackets(logSentPackets);
        bot.SetEnableLazyDecoding(lazyPacketSend);
        bot.SetSentPacketSize(sentPacketsSize);

        bot.SetLogger(BotLogger);

        bot.RegisterCommands();

        bot.Run();
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