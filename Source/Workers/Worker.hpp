#pragma once

#include <functional>
#include <future>

namespace FSDB
{
    template<class T>
    struct Worker
    {
    private:
        static void DefaultDeleter() noexcept {}
    public:
        Worker(const size_t& index, std::function<T()>&& work)
            :m_Index(index), m_HasWorkBeenStarted(false), m_Work(std::move(work)) {}
        Worker(Worker&& other) noexcept
            : m_Index(std::move(other.m_Index)), m_HasWorkBeenStarted(std::move(other.m_HasWorkBeenStarted)), m_Future(std::move(other.m_Future)), m_Work(std::move(other.m_Work)) {}

        Worker& operator=(Worker&& other) noexcept
        {
            m_Index = std::move(other.m_Index);
            m_Future = std::move(other.m_Future);
            m_Work = std::move(other.m_Work);
            m_HasWorkBeenStarted = std::move(other.m_HasWorkBeenStarted);

            return *this;
        }
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