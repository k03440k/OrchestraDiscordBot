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
        constexpr std::string_view resourcesPathStringView = "Resources";

        std::filesystem::path resourcesPath = resourcesPathStringView;

        std::filesystem::path path{ argv[0] };
        path.remove_filename();

        const GuelderResourcesManager::ResourcesManager resourcesManager{ path };
        const GuelderResourcesManager::ConfigFile mainConfig{ resourcesManager.GetFullPathToRelativeFile(resourcesPath / "Main.cfg") };

        std::filesystem::path globalPathToYt_dlpExecutable = mainConfig.GetVariable("globalPathToYt_dlpExecutable").GetValue<std::string>();
        std::filesystem::path commandsNamesConfigPath = path / resourcesPath / mainConfig.GetVariable("localPathToCommandsNamesConfig").GetValue<std::string>();
        std::filesystem::path commandsDescriptionsConfigPath = path / resourcesPath / mainConfig.GetVariable("localPathToCommandsDescriptionsConfig").GetValue<std::string>();
        std::filesystem::path guildsConfigPath = path / resourcesPath / mainConfig.GetVariable("localPathToGuildsConfig").GetValue<std::string>();
        std::filesystem::path historyLogPath;

        try
        {
            auto value = mainConfig.GetVariable("localPathToHistoryLog").GetValue<std::string>();

            if(!value.empty())
                historyLogPath = path / resourcesPath / value;
        }
        catch(...) {}

        auto botToken = mainConfig.GetVariable("botToken").GetValue<std::string>();

        unsigned long long bossSnowflake = 0;
        unsigned int sentPacketsSize = 20000;
        bool enableLogSentPackets = false;
        std::string commandsPrefix;
        char paramsPrefix = '-';
        uint32_t maxDownloadFileSize = 0;

        try
        {
            sentPacketsSize = mainConfig.GetVariable("sentPacketsSize").GetValue<unsigned int>();
        }
        catch(...) {}
        try
        {
            enableLogSentPackets = mainConfig.GetVariable("enableLoggingSentPackets").GetValue<bool>();
        }
        catch(...) {}
        try
        {
            commandsPrefix = mainConfig.GetVariable("commandsPrefix").GetValue<std::string>();
        }
        catch(...) { commandsPrefix = "!"; }
        try
        {
            paramsPrefix = mainConfig.GetVariable("paramsPrefix").GetRawValue()[0];
        }
        catch(...) {}

        try
        {
            bossSnowflake = mainConfig.GetVariable("bossSnowflake").GetValue<unsigned long long>();
        }
        catch(...)
        {
            LogWarning("The variable \"bossSnowflake\" is empty, setting bossSnowflake to 0.");
        }
        try
        {
            maxDownloadFileSize = mainConfig.GetVariable("maxDownloadFileSize").GetValue<uint32_t>();
        }
        catch(...)
        {
            maxDownloadFileSize = std::numeric_limits<uint32_t>::max();
        }

        OrchestraDiscordBot bot
        {
            botToken,
            OrchestraDiscordBot::Paths
            {
                std::move(path),
                std::move(commandsNamesConfigPath),
                std::move(commandsDescriptionsConfigPath),
                std::move(historyLogPath),
                std::move(guildsConfigPath),
                std::move(globalPathToYt_dlpExecutable)
            },
            FullOrchestraDiscordBotInstanceProperties
            {
                sentPacketsSize,
                enableLogSentPackets,

                OrchestraDiscordBotInstanceProperties
                {
                    std::move(commandsPrefix),
                    paramsPrefix,
                    maxDownloadFileSize,
                    0
                }
            },
            bossSnowflake
        };

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