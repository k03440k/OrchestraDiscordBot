#pragma once

#include <GuelderConsoleLog.hpp>
#include <string_view>
#include <memory>

namespace Orchestra
{
    GE_DECLARE_LOG_CATEGORY_EXTERN(Orchestra, All, true, false, true);
    inline GE_DEFINE_LOG_CATEGORY(Orchestra);

    template<typename T, typename D>
    std::unique_ptr<T, D> CloneUniquePtr(const std::unique_ptr<T, D>& other)
    {
        if(other)
            return std::unique_ptr<T, D>(new T(*other), other.get_deleter());
        else
            return std::unique_ptr<T, D>(nullptr, other.get_deleter());
    }
    inline bool StringToBool(const std::string_view& str)
    {
        if(str == "true" || str == "1")
            return true;
        else if(str == "false" || str == "0")
            return false;
        else
            GE_THROW("Failed to convert string to bool");
    }
    inline int StringToInt(const std::string_view& str)
    {
        return std::stoi(str.data());
    }
    inline float StringToFloat(const std::string_view& str)
    {
        return std::stof(str.data());
    }
}