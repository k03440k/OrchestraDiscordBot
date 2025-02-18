#include "DiscordBot.hpp"

#include <string_view>
#include <vector>
#include <functional>
#include <cwctype>

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

                        auto foundPrefix = std::ranges::search(content, m_Prefix);

                        if(foundPrefix.begin() == content.begin())
                        {
                            size_t commandOffset = foundPrefix.size();

                            ParsedCommandWithIndex tmp = ParseCommand(m_Commands, content, commandOffset);

                            ParsedCommand parsedCommand = std::move(tmp.parsedCommand);
                            size_t commandIndex = tmp.index;

                            auto command = m_Commands.begin() + commandIndex;

                            GE_LOG(Orchestra, Info, "User with snowflake: ", message.msg.author.id, " has just called \"", parsedCommand.name, "\" command.");

                            auto f = [command, message, parsedCommand] { (*command)(message, parsedCommand.params, parsedCommand.value); };

                            size_t id = m_WorkersManger.AddWorker(std::move(f), true);

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
    DiscordBot::ParsedCommandWithIndex DiscordBot::ParseCommand(const std::vector<Command>& supportedCommands, const std::string_view& message, const size_t& commandOffset)
    {
        using CommandsIterator = decltype(supportedCommands.begin());
        using MessageIterator = decltype(message.begin());

        std::string commandName;
        size_t commandEndOffset = commandOffset;
        CommandsIterator foundSupportedCommand;
        size_t supportedCommandIndex = -1;
        //find command name
        {
            size_t commandLength = message.find(' ', commandOffset) - commandOffset;
            commandName = message.substr(commandOffset, commandLength);

            foundSupportedCommand = std::ranges::find_if(supportedCommands, [&commandName](const Command& _command) { return commandName == _command.name; });
            GE_ASSERT(foundSupportedCommand != supportedCommands.end(), "Failed to find command with name \"", commandName, "\".");

            commandEndOffset += commandLength + 1;
            supportedCommandIndex = foundSupportedCommand - supportedCommands.begin();
        }

        //params
        size_t commandValueOffset = 0;
        std::vector<Param> params;
        if(commandEndOffset)
        {
            //firstly it finds param count or at least tries
            size_t paramsCount = 0;
            size_t paramsValuesCount = 0;

            //first its name, second its value(if exists)
            std::vector<std::pair<size_t, size_t>> paramsOffsets;

            //finds param count and their values count or at least tries
            for(size_t i = commandEndOffset; i < message.size(); i++)
            {
                //setting its name
                if(message[i - 1] == ' ' && message[i] == '-' && i + 1 < message.size() && IsParamNameChar(message[i + 1]))
                {
                    paramsValuesCount = 0;
                    paramsCount++;

                    paramsOffsets.emplace_back(i + 1, std::string::npos);
                }
                //setting its value
                else if(message[i - 1] == ' ' && IsParamValueChar(message[i]))
                {
                    if(!paramsCount)
                    {
                        commandValueOffset = i;
                        break;
                    }

                    paramsValuesCount++;

                    if(paramsValuesCount == 1)
                        paramsOffsets[paramsOffsets.size() - 1].second = i;
                }

                if(paramsValuesCount >= 2)
                {
                    commandValueOffset = i;

                    break;
                }
            }

            if(paramsCount)
            {
                params.reserve(paramsCount);

                for(size_t i = 0; i < paramsOffsets.size(); i++)
                {
                    size_t paramLength = message.find(' ', paramsOffsets[i].first) - paramsOffsets[i].first;
                    std::string paramName{ message.substr(paramsOffsets[i].first, paramLength) };

                    auto foundParamProperty = std::ranges::find_if(foundSupportedCommand->paramsProperties, [&paramName](const ParamProperties& paramProperties) { return paramName == paramProperties.name; });
                    //if param name is ill-formed
                    if(foundParamProperty == foundSupportedCommand->paramsProperties.end())
                        continue;

                    size_t paramValueLength = message.find(' ', paramsOffsets[i].second) - paramsOffsets[i].second;
                    std::string paramValue;
                    //probably bool, like "-search"
                    if(foundParamProperty->type == Type::Bool && paramValueLength == 0)
                        paramValue = "1";
                    else
                        paramValue = (!paramValueLength ? "" : message.substr(paramsOffsets[i].second, paramValueLength));

                    Param param{ ParamProperties{std::move(paramName), foundParamProperty->type}, std::move(paramValue) };

                    try
                    {
                        //GetValue can throw an error so checking whether a value of param is ill-formed
                        switch(param.properties.type)
                        {
                        case Type::Int:
                            param.GetValue<int>();
                            break;
                        case Type::Float:
                            param.GetValue<float>();
                            break;
                        case Type::Bool:
                            param.GetValue<bool>();
                            break;
                        case Type::String:
                            param.GetValue<std::string>();
                            break;
                        }
                    }
                    catch(...)
                    {
                        if(i == paramsOffsets.size() - 1)
                            commandValueOffset = paramsOffsets[i].second;

                        if(param.properties.type == Type::Bool)
                            param.value = "1";
                        else
                            continue;
                    }

                    params.push_back(std::move(param));
                }

                //make only one instance of params with same names
                std::sort(params.begin(), params.end());
                params.erase(std::ranges::unique(params, [](const Param& left, const Param& right) { return left.properties.name == right.properties.name; }).begin(), params.end());
            }
        }

        //command value
        MessageIterator lastWhiteSpace = std::find_if(message.rbegin(), std::reverse_iterator{ message.begin() + commandValueOffset }, [](const char& ch) { return ch != ' '; }).base();
        size_t commandValueLength = lastWhiteSpace - message.begin() - commandValueOffset;

        std::string commandValue{ commandValueOffset == 0 ? "" : message.substr(commandValueOffset, commandValueLength) };

        return { {std::move(commandName), std::move(params), std::move(commandValue)}, supportedCommandIndex };
    }
    bool DiscordBot::IsParamNameChar(const char& ch)
    {
        return std::iswalpha(CharToWChar(ch)) || ch == '_';
    }
    bool DiscordBot::IsParamValueChar(const char& ch)
    {
        return std::iswalnum(CharToWChar(ch)) || ch == '.' || ch == ',' || ch == '-';
    }
}