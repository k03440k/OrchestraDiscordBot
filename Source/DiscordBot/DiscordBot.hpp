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
    GE_DECLARE_LOG_CATEGORY_EXTERN(DPP, All, true, false, true);

    class DiscordBot : protected dpp::cluster
    {
    public:
        DiscordBot(const std::string_view& token, const std::string_view& prefix, uint32_t intents = dpp::i_all_intents);
        ~DiscordBot() override = default;

        void AddCommand(const Command& command);
        void AddCommand(Command&& command);
        void RegisterCommands();

        void Run();

        void SetLogger(const std::function<void(const dpp::log_t& log)>& logger);

    protected:
        const std::string m_Prefix;
        std::vector<Command> m_Commands;

        WorkersManager<void> m_WorkersManger;

    private:
        static ParsedCommand ParseCommand(const std::vector<Command>& supportedCommands, const std::string_view& prefix, const std::string_view& message);
    };
}