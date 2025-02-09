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

                    ParsedCommand parsedCommand = ParseCommand(m_Prefix, content);

                    const auto found = std::ranges::find_if(m_Commands, [&parsedCommand](const Command& command) { return command.name == parsedCommand.name; });

                    if(found != m_Commands.end())
                    {
                        LogInfo("User with snowflake: ", message.msg.author.id, " has just called \"", parsedCommand.name, "\" command.");

                        const size_t id = m_WorkersManger.AddWorker([&command = *found, message, parsedCommand] { command(message, parsedCommand.params, parsedCommand.value); }, true);

                        m_WorkersManger.Work(id);
                    }
                    /*for(auto& command : m_Commands)
                    {
                        const size_t found = content.find(command.name);
                        const size_t prefixSize = m_Prefix.size();

                        if(found != std::string::npos && found > 0 && found >= prefixSize && content.substr(found - prefixSize, prefixSize) == m_Prefix)
                        {
                            LogInfo("User with snowflake: ", message.msg.author.id, " has just called \"", command.name, "\" command.");

                            const size_t id = m_WorkersManger.AddWorker([command, message] { command(message); }, true);

                            m_WorkersManger.Work(id);
                        }
                    }*/
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
    ParsedCommand DiscordBot::ParseCommand(const std::string_view& prefix, const std::string_view& message)
    {
        using iterator = decltype(prefix.begin());

        const size_t foundPrefix = message.find(prefix);

        GE_ASSERT(foundPrefix != std::string::npos, "Failed to find prefix.");

        size_t index = 0;

        const iterator whiteSpaceAfterCommandName = std::find(message.begin() + foundPrefix + 1, message.end(), ' ');

        //GE_ASSERT(whiteSpaceAfterCommandName != message.end(), "Failed to distinguish a command name.");

        std::string name{ message.begin() + foundPrefix + 1, whiteSpaceAfterCommandName };

        const size_t _nexCommandFound = message.find(prefix, foundPrefix + 1);
        const size_t nextCommandOffset = (_nexCommandFound == std::string::npos ? 0 : _nexCommandFound - foundPrefix);
        const iterator whiteSpaceBeforeValue = std::find(std::find_if(message.rbegin() + nextCommandOffset, message.rend(), [](const char& ch) { return ch != ' '; }), std::reverse_iterator(whiteSpaceAfterCommandName), ' ').base();
        //const iterator whiteSpaceBeforeValue = std::find(message.rend().base() + foundPrefix, std::find_if(message.rend().base(), message.rbegin().base() + nextCommandOffset, [](const char& ch) { return ch != ' '; }), ' ');

        std::vector<Param> params;

        std::string value;

        if(whiteSpaceAfterCommandName != message.end())
        {
            const size_t paramsCount = std::count(whiteSpaceAfterCommandName, whiteSpaceBeforeValue, '-');

            params.reserve(paramsCount);

            if(paramsCount)
            {
                iterator current = std::find(message.begin() + index, message.end(), '-');

                for(size_t i = 0; i < paramsCount; ++i)
                {
                    iterator whiteSpaceAfterName = std::find(current, message.end(), ' ');

                    iterator value = std::find_if(whiteSpaceAfterName + 1, message.end(), [](const char& ch) { return ch != ' '; });

                    GE_ASSERT(value != whiteSpaceBeforeValue, "Failed to extract the param value, probably the value to the param was misspelled.");

                    iterator whiteSpaceAfterValue = std::find(value, message.end(), ' ');

                    params.emplace_back(std::string(current + 1, whiteSpaceAfterName), std::string(value, whiteSpaceAfterValue));

                    current = std::find(whiteSpaceAfterValue + 1, message.end(), '-');
                }

                std::sort(params.begin(), params.end());
                params.erase(std::unique(params.begin(), params.end()), params.end());

            }
            else
            {
                iterator wrongValue = std::find_if(whiteSpaceAfterCommandName, whiteSpaceBeforeValue, [](const char& ch) { return ch != ' '; });

                GE_ASSERT(wrongValue == whiteSpaceBeforeValue, "There is an extra value between command name and its value.");
            }

            value = { whiteSpaceBeforeValue, std::find(whiteSpaceBeforeValue, message.end(), ' ') };
        }

        return { std::move(name), std::move(params), std::move(value) };
    }
}