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
                            RemoveWorkerAsync(index);
                    }
                    else
                    {
                        auto result = _func();

                        if(remove)
                            RemoveWorkerAsync(index);

                        return result;
                    }
                },
                [this, index = m_WorkersCurrentIndex.load(), remove, _exceptionDeleter = std::move(exceptionDeleter)](const _Exception& e)
                {
                    _exceptionDeleter(e);

                    if(remove)
                        RemoveWorkerAsync(index, e);
                },
                [this, index = m_WorkersCurrentIndex.load(), remove](const std::exception& e)
                {
                    if(remove)
                        RemoveWorkerAsync(index, e);
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

            GE_LOG(Orchestra, Info, "Deleted worker with index ", index, ". m_Workers.size() = ", m_Workers.size());
        }

        void Reserve(size_t reserve)
        {
            std::lock_guard lock{ m_WorkersMutex };

            m_Workers.reserve(reserve);
        }

        const WorkerType& GetWorker(size_t index) const
        {
            std::lock_guard lock{ m_WorkersMutex };

            auto found = std::ranges::find_if(m_Workers, [&index](const WorkerType& worker) { return worker.GetIndex() == index; });

            if(found != m_Workers.end())
                return *found;
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
        template<GuelderConsoleLog::Concepts::IsException _ExceptionType = _Exception>
            requires requires (_ExceptionType e)
        {
            e.what();
        }
        void RemoveWorkerAsync(size_t index, _ExceptionType exception)
        {
            std::thread removeThread([this, index, _exception = std::move(exception)]
                {
                    RemoveWorker(index);
                    GE_LOG(Orchestra, Error, "Deleted worker with index ", index, " because an ", typeid(_ExceptionType).name(), " occured: ", _exception.what());
                });
            removeThread.detach();
        }
        void RemoveWorkerAsync(size_t index)
        {
            std::thread removeThread([this, index]
                {
                    RemoveWorker(index);
                    GE_LOG(Orchestra, Info, "Deleting worker with index ", index, '.');
                });
            removeThread.detach();
        }

    private:
        std::atomic<size_t> m_WorkersCurrentIndex;
        std::vector<WorkerType> m_Workers;
        mutable std::mutex m_WorkersMutex;
    };
}