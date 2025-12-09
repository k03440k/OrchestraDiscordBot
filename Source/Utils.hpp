#pragma once

#include <string_view>
#include <memory>
#include <regex>
#include <functional>
#include <chrono>

#include <GuelderConsoleLog.hpp>

#define O_EXCEPTION(userMessage, fullMessage) ::Orchestra::OrchestraException(userMessage, fullMessage)

#define O_ASSERT(condition, ...) GE_CASSERT(condition, O_EXCEPTION(::GuelderConsoleLog::Logger::Format(__VA_ARGS__), GE_MAKE_FULL_ERROR_STRING(::GuelderConsoleLog::Logger::Format(__VA_ARGS__))))
#define O_THROW(...) GE_CTHROW(O_EXCEPTION(::GuelderConsoleLog::Logger::Format(__VA_ARGS__), GE_MAKE_FULL_ERROR_STRING(::GuelderConsoleLog::Logger::Format(__VA_ARGS__))))

#define O_STRUCT_GUARD_NAME(name) GE_CONCATENATE(name, Guard)
#define O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(name) GE_CONCATENATE(name, BinarySemaphoreGuard)

//use it in scopes, so the lock gets destroyed
#define O_DEFINE_STRUCT_GUARD(dataTypeName, mutexType)\
    struct O_STRUCT_GUARD_NAME(dataTypeName)\
    {\
        public:\
        O_STRUCT_GUARD_NAME(dataTypeName)() = default;\
        O_STRUCT_GUARD_NAME(dataTypeName) (mutexType& _mutex, dataTypeName* _data)\
            : m_Lock(_mutex), m_Pointer(_data) { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been locked.");*/ }\
        O_STRUCT_GUARD_NAME(dataTypeName)(O_STRUCT_GUARD_NAME(dataTypeName)&& other) noexcept = default;\
        O_STRUCT_GUARD_NAME(dataTypeName)& operator=(O_STRUCT_GUARD_NAME(dataTypeName)&& other) noexcept = default;\
        ~O_STRUCT_GUARD_NAME(dataTypeName)() { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been deleted.");*/ }\
        dataTypeName* const operator->() { return m_Pointer; }\
        const dataTypeName* const operator->() const { return m_Pointer; }\
        dataTypeName* const operator*() const { return m_Pointer; }\
        void Unlock() { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been unlocked.");*/ m_Pointer = nullptr; m_Lock.unlock(); }\
        private:\
        std::unique_lock<mutexType> m_Lock;\
        dataTypeName* m_Pointer;\
    }
#define O_DEFINE_STRUCT_GUARD_STD_MUTEX(dataTypeName) O_DEFINE_STRUCT_GUARD(dataTypeName, std::mutex)

//use it in scopes, so the semaphore gets unlocked
#define O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE(dataTypeName)\
    struct O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)\
    {\
        public:\
        O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)() = default;\
        O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName) (std::binary_semaphore* binarySemaphore, dataTypeName* _data)\
            : m_BinarySemaphore(binarySemaphore), m_Pointer(_data) { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been locked.");*/ m_BinarySemaphore->acquire(); }\
        O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)(O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)&& other) noexcept = default;\
        O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)& operator=(O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)&& other) noexcept = default;\
        ~O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName)() { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been deleted.");*/ Unlock(); }\
        dataTypeName* const operator->() { return m_Pointer; }\
        const dataTypeName* const operator->() const { return m_Pointer; }\
        dataTypeName* const operator*() const { return m_Pointer; }\
        void Unlock() { /*::GuelderConsoleLog::LogWarning(m_Pointer, ' ', GE_TO_STRING(dataTypeName), " has been unlocked.");*/ m_Pointer = nullptr; m_BinarySemaphore->release(); }\
        private:\
        std::binary_semaphore* m_BinarySemaphore;\
        dataTypeName* m_Pointer;\
    }

#define O_DEFINE_STRUCT_GUARD_GETTER(dataTypeName, ptrDataName, syncObjectName) O_STRUCT_GUARD_NAME(dataTypeName) GE_CONCATENATE(Access, dataTypeName)() { return { syncObjectName, ptrDataName }; }
#define O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE_GETTER(dataTypeName, ptrDataName, syncObjectName) O_STRUCT_GUARD_BINARY_SEMAPHORE_NAME(dataTypeName) GE_CONCATENATE(AccessBinarySemaphore, dataTypeName)() { return { syncObjectName, ptrDataName }; }

namespace Orchestra
{
    GE_DECLARE_LOG_CATEGORY_DEFAULT_COLORS_CONSTEXPR(Orchestra, All, true, false, true);

    template<typename T>
    concept Container =
        requires(T a) {
            { std::begin(a) } -> std::input_or_output_iterator;
            { std::end(a) } -> std::input_or_output_iterator;
    };

    template<typename FunctionType, typename ReturnType, typename... Args>
    concept Function =
        requires(FunctionType function, Args... args)
    {
        {function(args...)} -> std::same_as<ReturnType>;
    };

    class OrchestraException : public std::exception
    {
    public:
        OrchestraException(const std::string_view& userMessage, const std::string_view& fullMessage = "")
            : m_UserMessage(userMessage), m_FullMessage((fullMessage.empty() ? userMessage : fullMessage)) {
        }

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
    inline bool IsValidURL(const std::string& url)
    {
        // Regular expression to match basic URL structure
        const std::regex urlPattern(R"((https?|ftp)://([A-Za-z0-9.-]+)(:[0-9]+)?(/.*)?)");

        // Return true if the URL matches the pattern, false otherwise
        return std::regex_match(url, urlPattern);
    }
    inline void WaitUntil(const std::function<bool()>& condition, const std::chrono::milliseconds& maxTime, const std::chrono::milliseconds& sleepFor = std::chrono::milliseconds(10))
    {
        const auto startedTime = std::chrono::steady_clock::now();

        while(true)
        {
            if(condition() || std::chrono::steady_clock::now() - startedTime >= maxTime)
                break;

            std::this_thread::sleep_for(sleepFor);
        }
    }
    template<Container ContainerType>
    void Transfer(ContainerType& container, const size_t& from, const size_t& to)
    {
        if(from < to)
            std::rotate(container.begin() + from, container.begin() + from + 1, container.begin() + to + 1);
        else
            std::rotate(container.begin() + to, container.begin() + from, container.begin() + from + 1);
    }
    inline bool IsSpecialChar(char ch)
    {
        return !(
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch < 0
            );
    }
}