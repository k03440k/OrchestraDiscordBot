#include "Command.hpp"

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

#include <GuelderConsoleLog.hpp>

namespace Orchestra
{
    Command::Command(std::string name, CommandCallback func, std::vector<ParamProperties> paramsProperties, std::string description)
        : name(std::move(name)), func(std::move(func)), paramsProperties(std::move(paramsProperties)), description(std::move(description)) {}

    void Command::operator()(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const
    {
        //check if input params have the same name and type
        for(auto&& inParam : params)
        {
            if(std::ranges::find(paramsProperties, inParam.properties) == paramsProperties.end())
                O_THROW("Failed to interpred parameter: ", inParam.properties.name, '.');

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