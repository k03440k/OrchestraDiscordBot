#include "DiscordBot.hpp"

namespace FSDB
{
    GE_DEFINE_LOG_CATEGORY(DPP);

    DiscordBot::DiscordBot(const std::string_view& token, const std::string_view& prefix, uint32_t intents)
        : dpp::cluster(token.data(), intents), m_Prefix(prefix) {}

    void DiscordBot::AddCommand(const Command& command)
    {
        m_Commands.push_back(command);
    }
    void DiscordBot::AddCommand(Command&& command)
    {
        m_Commands.push_back(std::move(command));
    }
    void DiscordBot::RegisterCommands()
    {
        m_WorkersManger.Reserve(m_Commands.size());

        on_message_create(
            [&](const dpp::message_create_t& message)
            {
                if(message.msg.author.id != me.id)
                {
                    const auto& content = message.msg.content;
                    for(auto& command : m_Commands)
                    {
                        const size_t found = content.find(command.name);
                        const size_t prefixSize = m_Prefix.size();

                        if(found != std::string::npos && found > 0 && found >= prefixSize && content.substr(found - prefixSize, prefixSize) == m_Prefix)
                        {
                            LogInfo("User with snowflake: ", message.msg.author.id, " has just called \"", command.name, "\" command.");

                            const size_t id = m_WorkersManger.AddWorker([command, message] { command(message); }, true);

                            m_WorkersManger.Work(id);
                        }
                    }
                }
            }
        );
    }
    void DiscordBot::Run()
    {
        set_websocket_protocol(dpp::ws_etf);

        start(dpp::st_wait);
    }
    void DiscordBot::SetLogger(const std::function<void(const dpp::log_t& log)>& logger)
    {
        this->on_log(logger);
    }
}