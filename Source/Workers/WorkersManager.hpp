#pragma once

#include <functional>
#include <vector>

#include <GuelderConsoleLog.hpp>

#include "../Utils.hpp"
#include "Worker.hpp"

namespace Orchestra
{
    template<typename T, GuelderConsoleLog::Concepts::IsException _Exception = std::exception>
    class WorkersManager
    {
    public:
        using WorkerType = Worker<T, _Exception>;
    public:
        explicit WorkersManager(size_t reserve = 0)
            : m_WorkersCurrentIndex(0)
        {
            Reserve(reserve);
        }

        void Work()
        {
            std::lock_guard lock{ m_WorkersMutex };
            std::ranges::for_each(m_Workers, [](WorkerType& w) { if(!w.HasWorkBeenStarted()) w.Work(); });
        }
        void Work(size_t index)
        {
            std::lock_guard lock{ m_WorkersMutex };
            const auto found = std::ranges::find_if(m_Workers, [&index](const WorkerType& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                found->Work();
        }

        //returns worker's id
        size_t AddWorker(std::function<T()> func, std::function<void(const _Exception&)> exceptionDeleter = [] {}, bool remove = false)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.emplace_back(
                m_WorkersCurrentIndex,
                [this, _func = std::move(func), index = m_WorkersCurrentIndex.load(), remove]
                {
                    GE_LOG(Orchestra, Info, "Adding worker with index ", index, '.');
                    if constexpr(std::is_same_v<T, void>)
                    {
                        _func();

                        if(remove)
                        {
                            std::thread removeThread([this, index] { RemoveWorker(index); });
                            removeThread.detach();
                        }

                        GE_LOG(Orchestra, Info, "Deleting worker with index ", index, '.');
                    }
                    else
                    {
                        auto result = _func();

                        if(remove)
                        {
                            std::thread removeThread([this, index] { RemoveWorker(index); });
                            removeThread.detach();
                        }

                        GE_LOG(Orchestra, Info, "Deleting worker with index ", index, '.');

                        return result;
                    }
                },
                [this, index = m_WorkersCurrentIndex.load(), remove, _exceptionDeleter = std::move(exceptionDeleter)](const _Exception& e)
                {
                    _exceptionDeleter(e);
                    if(remove)
                    {
                        std::thread removeThread([this, index] { RemoveWorker(index); });
                        removeThread.detach();
                    }
                    GE_LOG(Orchestra, Error, "Deleting worker with index ", index, " because an exception occured: ", e.what());
                }
            );
            ++m_WorkersCurrentIndex;

            return m_WorkersCurrentIndex - 1;
        }
        void RemoveWorker(size_t index)
        {
            std::lock_guard lock{ m_WorkersMutex };

            const auto found = std::ranges::find_if(m_Workers, [&index](const WorkerType& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                m_Workers.erase(found);
            else
                O_THROW("Failed to find worker with index ", index, '.');
        }

        void Reserve(size_t reserve)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.reserve(reserve);
        }

        //WARNING: it doesn't return the result instantly
        const std::vector<WorkerType>& GetWorkers() const noexcept
        {
            std::lock_guard lock{ m_WorkersMutex };
            return m_Workers;
        }
        size_t GetCurrentWorkerIndex() const
        {
            return m_WorkersCurrentIndex.load();
        }
        //I haven't tested this one so yeah
        T GetWorkerFutureResult(size_t index)
        {
            std::lock_guard lock{ m_WorkersMutex };

            auto found = std::ranges::find_if(m_Workers, [&index](const WorkerType& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                return found->GetFutureResult();
            else
                O_THROW("Failed to find worker with index ", index, '.');
        }

    private:
        std::atomic<size_t> m_WorkersCurrentIndex;
        std::vector<WorkerType> m_Workers;
        mutable std::mutex m_WorkersMutex;
    };
}