#pragma once

#include <GuelderConsoleLog.hpp>
#include <string_view>
#include <memory>
#include <regex>

#define O_EXCEPTION(userMessage, fullMessage) ::Orchestra::OrchestraException(userMessage, fullMessage)

#define O_ASSERT(condition, ...) GE_CASSERT(condition, O_EXCEPTION(::GuelderConsoleLog::Logger::Format(__VA_ARGS__), GE_MAKE_FULL_ERROR_STRING(::GuelderConsoleLog::Logger::Format(__VA_ARGS__))))
#define O_THROW(...) GE_CTHROW(O_EXCEPTION(::GuelderConsoleLog::Logger::Format(__VA_ARGS__), GE_MAKE_FULL_ERROR_STRING(::GuelderConsoleLog::Logger::Format(__VA_ARGS__))))

namespace Orchestra
{
    GE_DECLARE_LOG_CATEGORY_DEFAULT_COLORS_CONSTEXPR(Orchestra, All, true, false, true);

    class OrchestraException : public std::exception
    {
    public:
        OrchestraException(const std::string_view& userMessage, const std::string_view& fullMessage = "")
            : m_UserMessage(userMessage), m_FullMessage((fullMessage.empty() ? userMessage : fullMessage)) {}

        const std::string& GetUserMessage() const noexcept { return m_UserMessage; }
        const std::string& GetFullMessage() const noexcept { return m_FullMessage; }

        const char* what() const override
        {
            return GetFullMessage().data();
        }

    private:
        std::string m_UserMessage;
        std::string m_FullMessage;
    };
    template<typename T, typename D>
    std::unique_ptr<T, D> CloneUniquePtr(const std::unique_ptr<T, D>& other)
    {
        if(other)
            return std::unique_ptr<T, D>(new T(*other), other.get_deleter());
        else
            return std::unique_ptr<T, D>(nullptr, other.get_deleter());
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