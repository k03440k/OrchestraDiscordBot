#pragma once

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

#include <GuelderConsoleLog.hpp>

#include "GuelderResourcesManager.hpp"

#include "../Utils.hpp"

namespace Orchestra
{
    enum class Type : uint8_t
    {
        Int = 0,
        Float,
        Bool,
        String
    };

    struct ParamProperties
    {
    public:
        friend bool operator==(const ParamProperties& left, const ParamProperties& right)
        {
            return left.name == right.name && left.type == right.type;
        }
        friend bool operator<(const ParamProperties& left, const ParamProperties& right)
        {
            return left.name < right.name;
        }

        std::string name;
        Type type;
        std::string description;
    };
    struct Param
    {
    public:
        friend bool operator==(const Param& left, const Param& right)
        {
            return left.properties == right.properties && left.value == right.value;
        }
        friend bool operator<(const Param& left, const Param& right)
        {
            return left.properties < right.properties;
        }

        template<typename Return>
        Return GetValue() const
        {
            O_THROW("Unsupported type.");
        }
        template<GuelderResourcesManager::IsNumber Numeral>
        Numeral GetValue() const
        {
            if(!IsNumeral())
                throw std::invalid_argument(GuelderConsoleLog::Logger::Format("The param's type is not ", typeid(Numeral).name()));

            return GuelderResourcesManager::StringToNumber<Numeral>(value);
        }
        template<>
        bool GetValue() const
        {
            O_ASSERT(properties.type == Type::Bool, "The param's type is not ", typeid(bool).name());
            return GuelderResourcesManager::StringToBool(value);
        }
        template<>
        std::string GetValue() const
        {
            return value;
        }
        template<>
        std::string_view GetValue() const
        {
            return value;
        }

        bool IsNumeral() const
        {
            switch(properties.type)
            {
            case Type::Int:
            case Type::Float:
                return true;
            default: 
                return false;
            }
        }

        ParamProperties properties;
        std::string value;
    };
    struct ParsedCommand
    {
    public:
        std::string name;
        std::vector<Param> params;
        std::string value;
    };

    struct Command
    {
    public:
        using CommandCallback = std::function<void(const dpp::message_create_t& message, const std::vector<Param>&, const std::string_view&)>;
    public:
        Command(const std::string_view& name, const CommandCallback& func, const std::vector<ParamProperties>& paramsProperties = {}, const std::string_view& description = "");
        Command(std::string&& name, CommandCallback&& func, std::vector<ParamProperties>&& paramsProperties = {}, std::string&& description = "");
        ~Command() = default;

        Command(const Command& other) = default;
        Command(Command&& other) noexcept = default;

        void operator()(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const;

        CommandCallback func;
        std::string name;
        std::string description;
        std::vector<ParamProperties> paramsProperties;
    };

    inline auto GetParam(const std::vector<Param>& params, const std::string_view& name)
    {
        return std::ranges::find_if(params, [&name](const Param& param) { return param.properties.name == name; });
    }
    inline bool IsThereParam(const std::vector<Param>& params, const std::string_view& name)
    {
        return (GetParam(params, name) != params.end());
    }
    inline int GetParamIndex(const std::vector<Param>& params, const std::string_view& name)
    {
        if(const auto it = GetParam(params, name); it != params.end())
            return it - params.begin();
        else
            return -1;
    }
    template<typename Return>
    Return GetParamValue(const std::vector<Param>& params, const std::string_view& name)
    {
        const auto found = GetParam(params, name);

        return found->GetValue<Return>();
    }
    template<typename Return>
    Return GetParamValue(const std::vector<Param>& params, const size_t& index)
    {
        return params[index].GetValue<Return>();
    }
    template<typename T>
    void GetParamValue(const std::vector<Param>& params, const std::string_view& name, T& value)
    {
        auto found = GetParam(params, name);

        if(found != params.end())
            value = found->GetValue<T>();
    }
    inline std::string_view TypeToString(const Type& type)
    {
        switch(type)
        {
        case Type::Int:
            return "int";
        case Type::Float:
            return "float";
        case Type::Bool:
            return "bool";
        case Type::String:
            return "string";
        }
    }
}