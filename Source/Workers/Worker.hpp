#pragma once

#include <functional>
#include <future>

namespace Orchestra
{
    template<class T, GuelderConsoleLog::Concepts::IsException _Exception = std::exception>
    struct Worker
    {
    public:
        Worker(size_t index, std::function<T()> work, std::function<void(const _Exception&)> exceptionDeleter = []{})
            : m_Index(index), m_HasWorkBeenStarted(false),
        m_Work(
            [_work = std::move(work), _exceptionDeleter = std::move(exceptionDeleter)]
            {
                try
                {
                    _work();
                }
                catch(const _Exception& e)
                {
                    _exceptionDeleter(e);
                    throw e;
                }
                catch (const std::exception& e)
                {
                    GE_LOG(Orchestra, Warning, "Caught exception: ", e.what());
                    throw e;
                }
                catch(...)
                {
                    const auto e = std::exception{"Unknown exception"};
                    //_exceptionDeleter(e);
                    GE_LOG(Orchestra, Warning, "Caught unknown exception.");
                    throw e;
                }
            }) {}
        Worker(Worker&&) noexcept = default;
        Worker& operator=(Worker&&) noexcept = default;

        ~Worker()
        {
            if(m_Future.valid())
                m_Future.wait_for(std::chrono::milliseconds(0));
        }

        void Work()
        {
            m_HasWorkBeenStarted = true;
            m_Future = std::async(std::launch::async, m_Work);
        }

        //INDEX IS UNIQUE BUT CAN BE SHARED
        size_t GetIndex() const noexcept { return m_Index; }
        const std::future<T>& GetFuture() const { return m_Future; }
        T GetFutureResult() { return m_Future.get(); }
        //if true, the Worker is invalid and should be removed or moved
        bool HasWorkBeenStarted() const { return m_HasWorkBeenStarted; }

    private:
        size_t m_Index;
        bool m_HasWorkBeenStarted;

        std::future<T> m_Future;
        std::function<T()> m_Work;
    };
}