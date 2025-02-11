#include "Command.hpp"

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

#include <GuelderConsoleLog.hpp>

namespace Orchestra
{
    Command::Command(const std::string_view& name, const CommandCallback& func, const std::vector<ParamProperties>& paramsProperties, const std::string_view& description)
        : func(func), name(name), description(description), paramsProperties(paramsProperties) {}

    Command::Command(std::string&& name, CommandCallback&& func, std::vector<ParamProperties>&& paramsProperties, std::string&& description)
        : func(std::move(func)), name(std::move(name)), description(std::move(description)), paramsProperties(std::move(paramsProperties)) {}

    void Command::operator()(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const
    {
        //check if input params have the same name and type
        for(auto&& inParam : params)
        {
            if(std::ranges::find(paramsProperties, inParam.properties) == paramsProperties.end())
                GE_THROW("Failed to interpred parameter: ", inParam.properties.name, '.');

            //check if the param truly has its type
            switch(inParam.properties.type)
            {
            case Type::Int:
                inParam.GetValue<int>();
                break;
            case Type::Float:
                inParam.GetValue<float>();
                break;
            case Type::Bool:
                inParam.GetValue<bool>();
                break;
            case Type::String:
                inParam.GetValue<std::string>();
                break;
            }
        }

        func(message, params, value);
    }
}