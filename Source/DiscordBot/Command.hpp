#pragma once

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

#include <GuelderConsoleLog.hpp>

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
            GE_THROW("Unsupported type.");
        }
        template<>
        int GetValue() const
        {
            GE_ASSERT(properties.type == Type::Int, "The type of param is not ", typeid(int).name());
            return StringToInt(value);
        }
        template<>
        float GetValue() const
        {
            GE_ASSERT(properties.type == Type::Float, "The type of param is not ", typeid(float).name());
            return StringToFloat(value);
        }
        template<>
        bool GetValue() const
        {
            GE_ASSERT(properties.type == Type::Bool, "The type of param is not ", typeid(bool).name());
            return StringToBool(value);
        }
        template<>
        std::string GetValue() const
        {
            GE_ASSERT(properties.type == Type::String, "The type of param is not ", typeid(std::string).name());
            return value;
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
    inline auto GetParam(const std::vector<Param>& params, const std::string_view& name)
    {
        return std::ranges::find_if(params, [&name](const Param& param) { return param.properties.name == name; });
    }
    inline bool IsThereParam(const std::vector<Param>& params, const std::string_view& name)
    {
        return (GetParam(params, name) != params.end());
    }
    template<typename Return>
    Return GetParamValue(const std::vector<Param>& params, const std::string_view& name)
    {
        auto found = GetParam(params, name);

        return found->GetValue<Return>();
    }
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
}