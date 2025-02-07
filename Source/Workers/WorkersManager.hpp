#pragma once

#include <functional>
#include <future>
#include <vector>

#include <GuelderConsoleLog.hpp>

#include "../Utils.hpp"

#include "Worker.hpp"

namespace FSDB
{
    template<typename T>
    class WorkersManager
    {
    public:
        WorkersManager() = default;
        explicit WorkersManager(const size_t& reserve)
            : m_WorkersCurrentIndex(0)
        {
            Reserve(reserve);
        }

        void Work()
        {
            std::lock_guard lock{ m_WorkersMutex };
            std::ranges::for_each(m_Workers, [](Worker<T>& w) { if(!w.HasWorkBeenStarted()) w.Work(); });
        }
        void Work(const size_t& index)
        {
            std::lock_guard lock{ m_WorkersMutex };
            const auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                found->Work();
        }

        //returns workers id
        size_t AddWorker(const std::function<T()>& func, const bool& remove = false)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.emplace_back(
                m_WorkersCurrentIndex,
                [this, _func = func, index = m_WorkersCurrentIndex.load(), remove]
                {
                    if constexpr(std::is_same_v<T, void>)
                    {
                        _func();

                        if(remove)
                        {
                            std::thread removeThread([=] { RemoveWorker(index); });
                            removeThread.detach();
                        }
                    }
                    else
                    {
                        auto result = _func();

                        if(remove)
                        {
                            std::thread removeThread([=] { RemoveWorker(index); });
                            removeThread.detach();
                        }

                        return result;
                    }
                }
            );
            ++m_WorkersCurrentIndex;

            return m_WorkersCurrentIndex - 1;
        }
        //returns workers id
        size_t AddWorker(std::function<T()>&& func, const bool& remove = false)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.emplace_back(
                m_WorkersCurrentIndex,
                [this, _func = std::move(func), index = m_WorkersCurrentIndex.load(), remove]
                {
                    if constexpr(std::is_same_v<T, void>)
                    {
                        _func();

                        if(remove)
                        {
                            std::thread removeThread([=] { RemoveWorker(index); });
                            removeThread.detach();
                        }
                        LogInfo("ending ", index);
                    }
                    else
                    {
                        auto result = _func();

                        if(remove)
                        {
                            std::thread removeThread([=] { RemoveWorker(index); });
                            removeThread.detach();
                        }

                        LogInfo("ending ", index);

                        return result;
                    }
                }
            );
            ++m_WorkersCurrentIndex;

            return m_WorkersCurrentIndex - 1;
        }
        void RemoveWorker(const size_t& index)
        {
            std::lock_guard lock{ m_WorkersMutex };

            const auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                m_Workers.erase(found);
            else
                GE_THROW("Failed to find worker with index ", index, '.');
        }

        void Reserve(const size_t& reserve)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.reserve(reserve);
        }

        //WARNING: it doesn't return the result momentally
        const std::vector<Worker<T>>& GetWorkers() const noexcept
        {
            std::lock_guard lock{ m_WorkersMutex };
            return m_Workers;
        }
        size_t GetCurrentWorkerIndex() const
        {
            return m_WorkersCurrentIndex.load();
        }
        //I haven't tested so yea
        T GetWorkerFutureResult(const size_t& index)
        {
            std::lock_guard lock{ m_WorkersMutex };

            auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                return found->GetFutureResult();
            else
                GE_THROW("Failed to find worker with index ", index, '.');
        }

    private:
        std::atomic<size_t> m_WorkersCurrentIndex;
        std::vector<Worker<T>> m_Workers;
        mutable std::mutex m_WorkersMutex;
    };
}