#include "rtos.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>

#if !defined(__AVR_ATmega32U4__)
#error "This scheduler's context switch is implemented for the ATmega32U4 only"
#endif

#if F_CPU % 64000UL != 0 || F_CPU / 64000UL > 65536UL
#error "Timer1 cannot produce an exact 1 ms tick with a prescaler of 64"
#endif

namespace {

rtos::TaskBase* const* taskTable;
uint8_t taskCount;
// taskCount identifies the Arduino idle context.
uint8_t currentTask;
uint16_t idleStack;
volatile uint8_t missedDeadlines;

extern "C" void rtos_yield();

void taskEntry() {
  for (;;) {
    taskTable[currentTask]->function();
    cli();
    taskTable[currentTask]->active = false;
    rtos_yield();
  }
}

uint16_t initializeStack(rtos::TaskBase& task) {
  uint8_t* sp = task.stack + task.stackSize - 1;
  const uint16_t entry = reinterpret_cast<uint16_t>(taskEntry);

  // Match the return address and register frame consumed by restoreContext.
  *sp-- = static_cast<uint8_t>(entry);
  *sp-- = static_cast<uint8_t>(entry >> 8);
  *sp-- = 0;  // r0
  *sp-- = 0;  // SREG (interrupts remain disabled until RETI)
  for (uint8_t reg = 1; reg <= 31; ++reg) {
    *sp-- = 0;
  }
  return reinterpret_cast<uint16_t>(sp);
}

uint16_t selectNext(uint16_t savedStack) {
  if (currentTask == taskCount) {
    idleStack = savedStack;
  } else {
    taskTable[currentTask]->savedStack = savedStack;
  }

  for (uint8_t i = 0; i < taskCount; ++i) {
    if (taskTable[i]->active && taskTable[i]->waitingOn == nullptr) {
      currentTask = i;
      return taskTable[i]->savedStack;
    }
  }
  currentTask = taskCount;
  return idleStack;
}

}  // namespace

// These functions run on the interrupted task's stack with interrupts disabled.
extern "C" __attribute__((used, noinline)) uint16_t rtos_on_tick(uint16_t sp) {
  for (uint8_t i = 0; i < taskCount; ++i) {
    rtos::TaskBase& task = *taskTable[i];
    if (--task.remaining == 0) {
      task.remaining = task.period;
      if (task.active) {
        missedDeadlines |= static_cast<uint8_t>(1U << i);
      } else {
        task.active = true;
      }
    }
  }
  return selectNext(sp);
}

extern "C" __attribute__((used, noinline)) uint16_t rtos_on_yield(uint16_t sp) {
  return selectNext(sp);
}

// The timer ISR and the task-completion yield have the same saved frame.
// Both enter with interrupts disabled and resume via RETI.
#define SAVE_CONTEXT \
  "push r0\n\t" \
  "in r0, __SREG__\n\t" \
  "cli\n\t" \
  "push r0\n\t" \
  "push r1\n\t" \
  "clr r1\n\t" \
  "push r2\n\t" "push r3\n\t" "push r4\n\t" "push r5\n\t" \
  "push r6\n\t" "push r7\n\t" "push r8\n\t" "push r9\n\t" \
  "push r10\n\t" "push r11\n\t" "push r12\n\t" "push r13\n\t" \
  "push r14\n\t" "push r15\n\t" "push r16\n\t" "push r17\n\t" \
  "push r18\n\t" "push r19\n\t" "push r20\n\t" "push r21\n\t" \
  "push r22\n\t" "push r23\n\t" "push r24\n\t" "push r25\n\t" \
  "push r26\n\t" "push r27\n\t" "push r28\n\t" "push r29\n\t" \
  "push r30\n\t" "push r31\n\t" \
  "in r24, __SP_L__\n\t" \
  "in r25, __SP_H__\n\t"

#define RESTORE_CONTEXT \
  "out __SP_H__, r25\n\t" \
  "out __SP_L__, r24\n\t" \
  "pop r31\n\t" "pop r30\n\t" "pop r29\n\t" "pop r28\n\t" \
  "pop r27\n\t" "pop r26\n\t" "pop r25\n\t" "pop r24\n\t" \
  "pop r23\n\t" "pop r22\n\t" "pop r21\n\t" "pop r20\n\t" \
  "pop r19\n\t" "pop r18\n\t" "pop r17\n\t" "pop r16\n\t" \
  "pop r15\n\t" "pop r14\n\t" "pop r13\n\t" "pop r12\n\t" \
  "pop r11\n\t" "pop r10\n\t" "pop r9\n\t" "pop r8\n\t" \
  "pop r7\n\t" "pop r6\n\t" "pop r5\n\t" "pop r4\n\t" \
  "pop r3\n\t" "pop r2\n\t" "pop r1\n\t" \
  "pop r0\n\t" \
  "out __SREG__, r0\n\t" \
  "pop r0\n\t" \
  "reti\n\t"

ISR(TIMER1_COMPA_vect, ISR_NAKED) {
  asm volatile(SAVE_CONTEXT "call rtos_on_tick\n\t" RESTORE_CONTEXT);
}

extern "C" __attribute__((naked, used, noinline)) void rtos_yield() {
  asm volatile(SAVE_CONTEXT "call rtos_on_yield\n\t" RESTORE_CONTEXT);
}

namespace rtos {
namespace detail {

void start(TaskBase* const* tasks, uint8_t count) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    taskTable = tasks;
    taskCount = count;
    currentTask = count;
    missedDeadlines = 0;
    for (uint8_t i = 0; i < count; ++i) {
      tasks[i]->remaining = tasks[i]->period;
      tasks[i]->active = false;
      tasks[i]->waitingOn = nullptr;
      tasks[i]->savedStack = initializeStack(*tasks[i]);
    }

    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = F_CPU / 64UL / 1000UL - 1;
    TIFR1 = _BV(OCF1A);
    TIMSK1 = _BV(OCIE1A);
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
  }
}

}  // namespace detail

bool SemaphoreBase::take() {
  if (!(SREG & _BV(SREG_I))) {
    return false;
  }
  cli();
  if (currentTask >= taskCount) {
    sei();
    return false;
  }
  if (count_ != 0) {
    --count_;
  } else {
    taskTable[currentTask]->waitingOn = this;
    rtos_yield();
  }
  sei();
  return true;
}

bool SemaphoreBase::tryTake() {
  bool acquired = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (count_ != 0) {
      --count_;
      acquired = true;
    }
  }
  return acquired;
}

bool SemaphoreBase::giveLocked() {
  for (uint8_t i = 0; i < taskCount; ++i) {
    if (taskTable[i]->active && taskTable[i]->waitingOn == this) {
      taskTable[i]->waitingOn = nullptr;
      return true;
    }
  }
  if (count_ == maximum_) {
    return false;
  }
  ++count_;
  return true;
}

bool SemaphoreBase::give() {
  if (!(SREG & _BV(SREG_I))) {
    return false;
  }
  cli();
  const bool given = giveLocked();
  if (given && taskCount != 0) {
    rtos_yield();
  }
  sei();
  return given;
}

bool SemaphoreBase::giveFromISR() {
  bool given;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    given = giveLocked();
  }
  return given;
}

uint8_t takeMissedDeadlines() {
  uint8_t result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    result = missedDeadlines;
    missedDeadlines = 0;
  }
  return result;
}

}  // namespace rtos
