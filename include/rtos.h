#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rtos {

using TaskFunction = void (*)();

class SemaphoreBase;

struct TaskBase {
  TaskFunction function;
  uint8_t* stack;
  size_t stackSize;
  uint16_t period;
  volatile uint16_t remaining;
  volatile bool active;
  SemaphoreBase* volatile waitingOn;
  uint16_t savedStack;
};

// Each activation calls function once; the function must return.
template <uint16_t PeriodMs, size_t StackSize>
struct Task : TaskBase {
  static_assert(PeriodMs > 0, "Task period must be at least 1 ms");
  static_assert(StackSize >= 96, "Task stack must be at least 96 bytes");

  explicit Task(TaskFunction function)
      : TaskBase{function, stackStorage, StackSize, PeriodMs, PeriodMs, false, nullptr, 0} {}

 private:
  uint8_t stackStorage[StackSize];
};

class SemaphoreBase {
 public:
  SemaphoreBase(const SemaphoreBase&) = delete;
  SemaphoreBase& operator=(const SemaphoreBase&) = delete;

  // Blocking wait; false means the call was not made from a task with interrupts enabled.
  bool take();
  bool tryTake();
  // False means the semaphore was full or interrupts were disabled.
  bool give();
  // Does not switch tasks until the next scheduler invocation.
  bool giveFromISR();

 protected:
  SemaphoreBase(uint8_t maximum, uint8_t initial) : count_(initial), maximum_(maximum) {}

 private:
  bool giveLocked();
  uint8_t count_;
  const uint8_t maximum_;
};

template <uint8_t Maximum, uint8_t Initial = 0>
class Semaphore : public SemaphoreBase {
  static_assert(Maximum > 0, "Semaphore maximum must be at least 1");
  static_assert(Initial <= Maximum, "Initial semaphore count exceeds maximum");

 public:
  Semaphore() : SemaphoreBase(Maximum, Initial) {}
};

namespace detail {
void start(TaskBase* const* tasks, uint8_t count);
}

template <size_t Count>
void start(TaskBase* const (&tasks)[Count]) {
  static_assert(Count > 0 && Count <= 8, "Configure between 1 and 8 tasks");
  detail::start(tasks, Count);
}

// Bit n indicates that task n missed at least one release since the last call.
uint8_t takeMissedDeadlines();

}  // namespace rtos
