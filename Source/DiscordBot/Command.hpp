#pragma once

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

namespace FSDB
{
    struct Param
    {
    public:
        friend bool operator==(const Param& left, const Param& right)
        {
            return left.name == right.name;
        }
        friend bool operator<(const Param& left, const Param& right)
        {
            return left.name < right.name;
        }

        std::string name;
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
        Command(const std::string_view& name, const CommandCallback& func, const std::string_view& description = "");
        Command(std::string&& name, CommandCallback&& func, std::string&& description = "");
        ~Command() = default;

        Command(const Command& other) = default;
        Command(Command&& other) noexcept = default;

        void operator()(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const;

        const CommandCallback func;
        const std::string name;
        const std::string description;
    };
}