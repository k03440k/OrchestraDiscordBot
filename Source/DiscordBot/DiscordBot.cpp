#include "DiscordBot.hpp"

#include <string_view>
#include <vector>
#include <functional>

#include <dpp/dpp.h>

#include "Command.hpp"
#include "../Workers/WorkersManager.hpp"
#include "../Utils.hpp"

namespace Orchestra
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
                try
                {
                    if(message.msg.author.id != me.id)
                    {
                        const auto& content = message.msg.content;

                        if(content[0] != m_Prefix[0])
                            return;

                        ParsedCommand parsedCommand = ParseCommand(m_Commands, m_Prefix, content);

                        const auto found = std::ranges::find_if(m_Commands, [&parsedCommand](const Command& command) { return command.name == parsedCommand.name; });

                        if(found != m_Commands.end())
                        {
                            GE_LOG(Orchestra, Info, "User with snowflake: ", message.msg.author.id, " has just called \"", parsedCommand.name, "\" command.");
                            auto f = [found, message, parsedCommand] { (*found)(message, parsedCommand.params, parsedCommand.value); };
                            const size_t id = m_WorkersManger.AddWorker(std::move(f), true);

                            m_WorkersManger.Work(id);
                        }
                    }
                }
                catch(const std::exception& e)
                {
                    message.reply(e.what());
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
    ParsedCommand DiscordBot::ParseCommand(const std::vector<Command>& supportedCommands, const std::string_view& prefix, const std::string_view& message)
    {
        using Iterator = decltype(prefix.begin());

        const Iterator foundPrefix = std::ranges::search(message, prefix).begin();

        //whether we found prefix and if it is in the FIRST PLACE of message
        GE_ASSERT(foundPrefix == message.begin(), "Failed to find prefix.");

        //!play -speed .5 -repeat 10 URLURLURL
        //     ^
        const Iterator whiteSpaceAfterCommandName = std::find(foundPrefix, message.end(), ' ');

        //GE_ASSERT(whiteSpaceAfterCommandName != message.end(), "Failed to distinguish a command name.");

        std::string name{ foundPrefix + 1, whiteSpaceAfterCommandName };

        const auto foundCommand = std::ranges::find_if(supportedCommands, [&name](const Command& command) { return command.name == name; });

        GE_ASSERT(foundCommand != supportedCommands.end(), "Failed to find command with name: \"", name, "\".");

        //!play -speed .5 -repeat 10 URLURLURL
        //                          ^
        const auto lastNonWhiteSpace = std::find_if(message.rbegin(), message.rend(), [](const char& ch) { return ch != ' '; });
        Iterator whiteSpaceBeforeCommandValue = message.end();
        if(whiteSpaceAfterCommandName != message.end())
            whiteSpaceBeforeCommandValue = std::find(lastNonWhiteSpace + 1, std::reverse_iterator(whiteSpaceAfterCommandName), ' ').base() - 1;

        //check if commandValue is a value of param, for instance, !play -speed .5 -repeat 10
        {
            if(whiteSpaceBeforeCommandValue != message.end())
            {
                size_t letterCount = 0;
                for(Iterator i = whiteSpaceBeforeCommandValue; i != message.begin(); --i)
                {
                    if(*i != ' ' && *i != '-')
                        letterCount++;
                    //this is param value
                    else if(letterCount && *i == ' ')
                        break;
                    //this is param
                    else if(letterCount && *i == '-')
                    {
                        whiteSpaceBeforeCommandValue = message.end();
                        break;
                    }
                }
            }
        }

        //const iterator whiteSpaceBeforeValue = std::find(message.rend().base() + foundPrefix, std::find_if(message.rend().base(), message.rbegin().base() + nextCommandOffset, [](const char& ch) { return ch != ' '; }), ' ');

        std::vector<Param> params;

        std::string value;

        if(whiteSpaceAfterCommandName != message.end())
        {
            if(!supportedCommands.empty())
            {
                const size_t paramsCount = std::count(whiteSpaceAfterCommandName, whiteSpaceBeforeCommandValue, '-');

                params.reserve(paramsCount);

                Iterator current = std::find(whiteSpaceAfterCommandName, whiteSpaceBeforeCommandValue, '-');

                for(size_t i = 0; i < paramsCount; ++i)
                {
                    Iterator whiteSpaceAfterName = std::find(current, whiteSpaceBeforeCommandValue, ' ');

                    Iterator paramValue = std::find_if(whiteSpaceAfterName + 1, whiteSpaceBeforeCommandValue, [](const char& ch) { return ch != ' '; });

                    //GE_ASSERT(paramValue != commandValue, "Failed to extract the param value, probably the value to the param was misspelled.");

                    Iterator whiteSpaceAfterValue = std::find(paramValue, whiteSpaceBeforeCommandValue, ' ');

                    std::string _paramName{ current + 1, whiteSpaceAfterName };
                    const auto foundParam = std::ranges::find_if(foundCommand->paramsProperties, [&_paramName](const ParamProperties& properties) { return properties.name == _paramName; });
                    GE_ASSERT(foundParam != foundCommand->paramsProperties.end(), "Failed to distinguish a param with name \"", _paramName, "\".");

                    params.emplace_back(ParamProperties{ std::move(_paramName), foundParam->type }, std::string{ paramValue, whiteSpaceAfterValue });

                    if(whiteSpaceAfterValue != whiteSpaceBeforeCommandValue)
                        current = std::find(whiteSpaceAfterValue + 1, whiteSpaceBeforeCommandValue, '-');
                    else
                        break;
                }

                std::sort(params.begin(), params.end());
                params.erase(std::ranges::unique(params, [](const Param& left, const Param& right) { return left.properties.name == right.properties.name; }).begin(), params.end());
            }
            else
            {
                const Iterator wrongValue = std::find_if(whiteSpaceAfterCommandName, whiteSpaceBeforeCommandValue, [](const char& ch) { return ch != ' '; });

                GE_ASSERT(wrongValue == whiteSpaceBeforeCommandValue, "There is an extra character between command name and its value.");
            }

            if(whiteSpaceBeforeCommandValue != message.end())
                value = { whiteSpaceBeforeCommandValue + 1, std::find(whiteSpaceBeforeCommandValue + 1, message.end(), ' ') };
        }

        return { std::move(name), std::move(params), std::move(value) };
    }
}