#include "Command.hpp"

#include <string>
#include <functional>
#include <dpp/dispatcher.h>

namespace FSDB
{
    Command::Command(const std::string_view& name, const std::function<void(const dpp::message_create_t& message)>& func, const std::string_view& description)
        : func(func), name(name), description(description) {}

    Command::Command(std::string&& name, std::function<void(const dpp::message_create_t& message)>&& func, std::string&& description)
        : func(std::move(func)), name(std::move(name)), description(std::move(description)) {}

    void Command::operator()(const dpp::message_create_t& message) const
    {
        func(message);
    }
}