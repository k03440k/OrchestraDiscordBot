#pragma once

#include <GuelderConsoleLog.hpp>
#include <string_view>
#include <memory>
#include <regex>

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
    inline wchar_t CharToWChar(const char& ch)
    {
        wchar_t wch;
        std::mbtowc(&wch, &ch, 1);
        return wch;
    }
    inline bool IsValidURL(const std::string& url)
    {
        // Regular expression to match basic URL structure
        const std::regex urlPattern(R"((https?|ftp)://([A-Za-z0-9.-]+)(:[0-9]+)?(/.*)?)");

        // Return true if the URL matches the pattern, false otherwise
        return std::regex_match(url, urlPattern);
    }
}