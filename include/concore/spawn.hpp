#pragma once

#include "task.hpp"
#include "detail/task_system.hpp"

#include <initializer_list>

namespace concore {

namespace detail {

//! Defines an executor that can spawn tasks in the current worker queue.
struct spawn_executor {
    void operator()(task t) const { detail::task_system::instance().spawn(std::move(t)); }
};

//! Similar to a spawn_executor, but doesn't wake other workers
struct spawn_continuation_executor {
    void operator()(task t) const { detail::task_system::instance().spawn(std::move(t), false); }
};

} // namespace detail

inline namespace v1 {

//! Spawns a new task in the current worker thread.
//! It is assumed this is going to be called from within a task (within a worker thread); in this
//! case, the task will be added to the list of tasks for the current worker.
//!
//! If the 'wake_workers' parameter is true, this will attempt to wake up workers to ensure that the
//! task is executed as soon as possible. But, if this is "a continuation" of the parent task, this
//! may not make sense, as we are moving computation to a different thread; for such cases, one can
//! pass false to the wake_workers parameter.
inline void spawn(task&& t, bool wake_workers = true) {
    detail::task_system::instance().spawn(std::move(t), wake_workers);
}

//! Spawn one task, given a functor to be executed.
template <typename F>
inline void spawn(F&& ftor, bool wake_workers = true) {
    auto grp = task_group::current_task_group();
    detail::task_system::instance().spawn(task(std::forward<F>(ftor), grp), wake_workers);
}

//! Spawn multiple tasks, given the functors to be executed.
inline void spawn(std::initializer_list<task_function>&& ftors, bool wake_workers = true) {
    auto grp = task_group::current_task_group();
    int count = static_cast<int>(ftors.size());
    for (auto& ftor : ftors) {
        // wake_workers applies only to the last element; otherwise pass true
        bool cur_wake_workers = (count-- > 0 || wake_workers);
        detail::task_system::instance().spawn(task(std::move(ftor), grp), cur_wake_workers);
    }
}

template <typename F>
inline void spawn_and_wait(F&& ftor) {
    auto& tsys = detail::task_system::instance();
    auto worker_data = tsys.enter_worker();

    auto grp = task_group::create(task_group::current_task_group());
    tsys.spawn(task(std::forward<F>(ftor), grp), false);
    tsys.busy_wait_on(grp);

    tsys.exit_worker(worker_data);
}

inline void spawn_and_wait(std::initializer_list<task_function>&& ftors, bool wake_workers = true) {
    auto& tsys = detail::task_system::instance();
    auto worker_data = tsys.enter_worker();

    auto grp = task_group::create(task_group::current_task_group());
    int count = static_cast<int>(ftors.size());
    for (auto& ftor : ftors) {
        bool cur_wake_workers = count-- > 0; // don't wake on the last task
        tsys.spawn(task(std::move(ftor), grp), cur_wake_workers);
    }
    tsys.busy_wait_on(grp);

    tsys.exit_worker(worker_data);
}

//! Wait on all the tasks from the given group to finish.
//! Keeping a children task alive from the group will make this wait forever.
inline void wait(task_group& grp) { detail::task_system::instance().busy_wait_on(grp); }

//! Executor that spawns tasks instead of enqueueing them
constexpr auto spawn_executor = detail::spawn_executor{};
//! Executor that spawns tasks instead of enqueueing them; this doesn't wake up other workers, so
//! the task is assumed to be a continuation of the current work.
constexpr auto spawn_continuation_executor = detail::spawn_continuation_executor{};

} // namespace v1

} // namespace concore