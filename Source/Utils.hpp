#pragma once

#include <GuelderConsoleLog.hpp>
#include <string_view>
#include <memory>
#include <chrono>

using namespace GuelderConsoleLog;

namespace FSDB
{
    template<typename T, typename D>
    inline std::unique_ptr<T, D> CloneUniquePtr(const std::unique_ptr<T, D>& other)
    {
        if(other)
            return std::unique_ptr<T, D>(new T(*other), other.get_deleter());
        else
            return std::unique_ptr<T, D>(nullptr, other.get_deleter());
    }
    inline bool StringToBool(const std::string_view& str)
    {
        if(str == "true")
            return true;
        else if(str == "false")
            return false;
        else
            GE_THROW("Failed to convert string to bool");
    }
    inline int StringToInt(const std::string_view& str)
    {
        return std::atoi(str.data());
    }
    inline double StringToDouble(const std::string_view& str)
    {
        return std::atof(str.data());
    }
}