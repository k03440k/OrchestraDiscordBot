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
        DiscordBot(const std::string& token, uint32_t intents = dpp::i_all_intents);
        ~DiscordBot() override = default;

        void AddCommand(Command command);
        virtual void RegisterCommands() = 0;

        void Run();

        void SetLogger(std::function<void(const dpp::log_t& log)> logger);

    protected:
        struct ParsedCommandWithIndex
        {
            ParsedCommand parsedCommand;
            size_t index;
        };

        using CommandPrefixGetter = std::function<std::string(const dpp::message_create_t& message)>;
        using ParamPrefixGetter = std::function<char(const dpp::message_create_t& message)>;
        using CommandChecker = std::function<bool(const dpp::message_create_t& message, ParsedCommandWithIndex&)>;

        static ParsedCommandWithIndex ParseCommand(const std::vector<Command>& supportedCommands, const std::string_view& message, size_t commandOffset = 0, char paramNamePrefix = '-');

    protected:
        std::vector<Command> m_Commands;

        WorkersManager<void, OrchestraException> m_WorkersManger;

    private:
        static bool IsValidParamNameChar(char ch);
        static bool IsValidParamNameBeginningChar(char ch);
        static bool IsValidParamValueChar(char ch);
    };
}