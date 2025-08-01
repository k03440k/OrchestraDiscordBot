#pragma once

#include <string_view>
#include <vector>
#include <functional>

#include <dpp/dpp.h>

#include "Command.hpp"
#include "../Workers/WorkersManager.hpp"
#include "../Utils.hpp"

namespace Orchestra
{
    GE_DECLARE_LOG_CATEGORY_DEFAULT_COLORS_CONSTEXPR(DPP, All, true, false, true);

    constexpr size_t DPP_MAX_MESSAGE_LENGTH = 2000;
    constexpr size_t DPP_MAX_EMBED_SIZE = 25;

    class DiscordBot : protected dpp::cluster
    {
    public:
        DiscordBot(const std::string& token, std::string commandPrefix, char paramNamePrefix = '-', uint32_t intents = dpp::i_all_intents);
        ~DiscordBot() override = default;

        void AddCommand(Command command);
        void RegisterCommands();

        void Run();

        void SetLogger(std::function<void(const dpp::log_t& log)> logger);

    protected:
        std::string m_Prefix;
        const char m_ParamNamePrefix;
        std::vector<Command> m_Commands;

        WorkersManager<void, OrchestraException> m_WorkersManger;

    private:
        struct ParsedCommandWithIndex
        {
            ParsedCommand parsedCommand;
            size_t index;
        };

    private:
        static ParsedCommandWithIndex ParseCommand(const std::vector<Command>& supportedCommands, const std::string_view& message, size_t commandOffset = 0, char paramNamePrefix = '-');
        static bool IsValidParamNameChar(char ch);
        static bool IsValidParamValueChar(char ch);
    };
}