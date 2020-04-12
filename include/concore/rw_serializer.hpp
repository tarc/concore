#pragma once

#include "task.hpp"
#include "executor_type.hpp"
#include "data/concurrent_queue.hpp"
#include "detail/utils.hpp"

#include <memory>

namespace concore {

inline namespace v1 {

/**
 * @brief      Similar to a serializer but allows two types of tasks: READ and WRITE tasks.
 * 
 * This class is not an executor. It binds together two executors: one for *READ* tasks and one for
 * *WRITE* tasks. This class adds constraints between *READ* and *WRITE* threads.
 * 
 * The *READ* tasks can be run in parallel with other *READ* tasks, but not with *WRITE* tasks. The
 * *WRITE* tasks cannot be run in parallel neither with *READ* nor with *WRITE* tasks.
 * 
 * This class provides two methods to access to the two executors: @ref read() and @ref write().
 * The @ref read() executor should be used to enqueue *READ* tasks while the @ref write() executor
 * should be used to enqueue *WRITE* tasks.
 * 
 * This implementation slightly favors the *WRITEs*: if there are multiple pending *WRITEs* and
 * multiple pending *READs*, this will execute all the *WRITEs* before executing the *READs*. The
 * rationale is twofold:
 *     - it's expected that the number of *WRITEs* is somehow smaller than the number of *READs*
 *     (otherwise a simple serializer would probably work too)
 *     - it's expected that the *READs* would want to *read* the latest data published by the
 *     *WRITEs*, so they are aiming to get the latest *WRITE*
 *     
 * **Guarantees**:
 *  - no more than 1 *WRITE* task is executed at once
 *  - no *READ* task is executed in parallel with other *WRITE* task
 *  - the *WRITE* tasks are executed in the order of enqueueing
 *  
 * @see        reader_type, writer_type, serializer, rw_serializer
 */
class rw_serializer {
    struct impl;
    //! Implementation detail shared by both reader and writer types
    std::shared_ptr<impl> impl_;

public:
    /**
     * @brief      The type of the executor used for *READ* tasks.
     * 
     * Objects of this type will be created by @ref rw_serializer to allow enqueueing *READ* tasks
     */
    class reader_type {
        std::shared_ptr<impl> impl_;

    public:
        //! Constructor. Should only be called by @ref rw_serializer
        reader_type(std::shared_ptr<impl> impl);
        /**
         * @brief      Function call operator.
         *
         * @param      t     The *READ* task to be enqueued
         * 
         * Depending on the state of the parent @ref rw_serializer object this will enqueue the task
         * immediately (if there are no *WRITE* tasks), or it will place it in a waiting list to be
         * executed later. The tasks on the waiting lists will be enqueued once there are no more
         * *WRITE* tasks.
         */
        void operator()(task t);
    };

    /**
     * @brief      The type of the executor used for *WRITE* tasks.
     * 
     * Objects of this type will be created by @ref rw_serializer to allow enqueueing *WRITE* tasks
     */
    class writer_type {
        std::shared_ptr<impl> impl_;

    public:
        //! Constructor. Should only be called by @ref rw_serializer
        writer_type(std::shared_ptr<impl> impl);
        /**
         * @brief      Function call operator.
         *
         * @param      t     The *WRITE* task to be enqueued
         * 
         * Depending on the state of the parent @ref rw_serializer object this will enqueue the task
         * immediately (if there are no other tasks executing), or it will place it in a waiting
         * list to be executed later. The tasks on the waiting lists will be enqueued, in order, one
         * by one. No new *READ* tasks are executed while we have *WRITE* tasks in the waiting list.
         */
        void operator()(task t);
    };

    /**
     * @brief      Constructor
     *
     * @param      base_executor  Executor to be used to enqueue new tasks
     * @param      cont_executor  Executor that enqueues follow-up tasks
     * 
     * If `base_executor` is not given, @ref global_executor will be used.
     * If `cont_executor` is not given, it will use `base_executor` if given, otherwise it will use
     * @ref spawn_continuation_executor for enqueueing continuations.
     * 
     * The first executor is used whenever new tasks are enqueued, and no task is in the wait list.
     * The second executor is used whenever a task is completed and we need to continue with the
     * enqueueing of another task. In this case, the default, @ref spawn_continuation_executor tends
     * to work better than @ref global_executor, as the next task is picked up immediately by the
     * current working thread, instead of going over the most general flow.
     * 
     * @see        global_executor, spawn_continuation_executor
     */
    explicit rw_serializer(executor_t base_executor = {}, executor_t cont_executor = {});

    /**
     * @brief      Returns an executor to enqueue *READ* tasks.
     *
     * @return     The executor for *READ* types
     */
    reader_type reader() const { return reader_type(impl_); }
    /**
     * @brief      Returns an executor to enqueue *WRITE* tasks.
     *
     * @return     The executor for *WRITE* types
     */
    writer_type writer() const { return writer_type(impl_); }
};

} // namespace v1
} // namespace concore