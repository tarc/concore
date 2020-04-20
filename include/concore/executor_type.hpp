#pragma once

#include "task.hpp"

#include <functional>

namespace concore {

/**
 * @brief Generic executor type.
 * 
 * This is a type-erasure, allowing all executor types to be stored and manipulated.
 * 
 * An executor is an abstraction that takes a task and schedules its execution, typically at a later
 * time, and maybe with certain constraints.
 * 
 * It is assumed that multiple tasks/threads can call the executor at the same time to enqueue tasks
 * with it.
 * 
 * @see global_executor, immediate_executor, serializer
 */
using executor_t = std::function<void(task)>;

} // namespace concore