#pragma once

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

namespace FSDB
{
    struct Command
    {
    public:
        Command(const std::string_view& name, const std::function<void(const dpp::message_create_t& message)>& func, const std::string_view& description = "");
        Command(std::string&& name, std::function<void(const dpp::message_create_t& message)>&& func, std::string&& description = "");
        ~Command() = default;

        Command(const Command& other) = default;
        Command(Command&& other) noexcept = default;

        void operator()(const dpp::message_create_t& message) const;

        const std::function<void(const dpp::message_create_t& message)> func;
        const std::string name;
        const std::string description;
    };
}