# Timer-driven task scheduler

This project implements a small, preemptive periodic scheduler for the
16 MHz ATmega32U4 Pro Micro. Timer1 generates a 1 ms interrupt; the scheduler
switches between separate, statically allocated task stacks. Arduino's `loop()`
is the idle context.

Define each task as a function that returns, give it a period in milliseconds
and a stack size in bytes, and add it to the task table in `src/main.cpp`:

```cpp
void sampleSensors() {
  // Do one bounded unit of work.
}

rtos::Task<10, 256> sensors(sampleSensors);
rtos::TaskBase* const tasks[] = {&sensors};

void setup() {
  rtos::start(tasks);
}
```

Periods are compile-time values from 1 to 65535 ms; the first release occurs
after one full period. Up to eight tasks are supported. Table order determines
fixed priority, with the first task highest. A higher-priority ready task
preempts a lower-priority task at the next tick. Tasks run until they return;
afterward they wait for their next release. Long-running high-priority tasks
can starve lower-priority tasks.

Use `rtos::suspend(task)` to stop a registered task from being scheduled and
`rtos::resume(task)` to make it eligible again. Both functions require
interrupts enabled and return `false` for an unregistered task or when called
with interrupts disabled. Suspension preserves the task's current execution
context, including a task blocked on a semaphore. Periodic releases continue
while suspended; releases that occur during an unfinished activation are
reported as missed deadlines as usual. Suspending or resuming a task takes
effect immediately through a scheduler invocation.
Inside a task, `rtos::suspend()` suspends the calling task; it returns `false`
outside a task or with interrupts disabled. It resumes at that call after
another task calls `rtos::resume()` for it.

If a task has not returned by its next release, that release is dropped rather
than queued. `rtos::takeMissedDeadlines()` atomically returns and clears a
bitmask of affected task indices. The example records this mask in
`missedDeadlines`; applications should monitor it and respond appropriately.

Call `rtos::start()` once, from `setup()`, with interrupts enabled. The task
table and task objects must remain alive permanently. Each task needs enough
stack for its deepest call chain, interrupt handlers, and the scheduler's
context frame; the 96-byte minimum is not a guarantee of safety. There is no
stack overflow detection. The main/idle stack also needs enough space for
interrupts and the scheduler. The ATmega32U4 has limited SRAM.

Tasks share memory and peripherals: protect shared multi-byte data and
non-reentrant resources against concurrent access. Do not leave interrupts
disabled when a task returns; avoid long critical sections and blocking calls
other than the semaphore wait described below.
Timer1 is exclusively owned by the scheduler, so Timer1-based libraries and
PWM cannot be used concurrently. Interrupts cannot preempt other interrupt
handlers on this target. If interrupts are disabled for longer than a tick,
hardware timer events can be lost and timing/deadline reporting becomes
inaccurate. This is a minimal scheduler, not a full-featured RTOS; in
particular, it has no stack overflow protection.

## Semaphores

`rtos::Semaphore<maximum, initial>` is a counting semaphore. Both values are
compile-time constants from 0 to 255; maximum must be at least 1, and initial
defaults to 0. For a binary semaphore use a maximum of 1:

```cpp
rtos::Semaphore<1, 1> resource;

void task() {
  if (!resource.take()) {
    // Report invalid calling context.
    return;
  }
  // Use the shared resource.
  if (!resource.give()) {
    // Report an invalid context or an over-release.
  }
}
```

- `take()` consumes a permit or blocks the calling task until a permit is
  handed to it. It returns `false` if called outside a task or with interrupts
  disabled. There is no timeout.
- `tryTake()` consumes a permit if available and returns immediately. It is
  safe from tasks, idle code and interrupt handlers.
- `give()` releases a permit and immediately schedules any higher-priority
  ready task. It requires interrupts enabled and returns `false` if the
  semaphore is already full or interrupts are disabled.
- `giveFromISR()` is safe in an interrupt handler. It returns `false` when
  full, preserves the interrupt state, and never switches tasks inside the
  handler. A woken task runs at the next scheduler invocation, normally the
  next 1 ms tick.

For example, an interrupt callback can signal a task:

```cpp
rtos::Semaphore<4> events;
volatile bool eventOverflow = false;

void onExternalInterrupt() {  // Register this callback with attachInterrupt().
  if (!events.giveFromISR()) {
    eventOverflow = true;
  }
}

void eventTask() {
  if (!events.take()) {
    // Report invalid calling context.
    return;
  }
  // Process one event.
}
```

When there are waiters, a permit is handed directly to the highest-priority
waiting task. A blocked task remains active: releases that occur before it
finishes are dropped and recorded as missed deadlines. Semaphore objects must
remain alive while tasks or interrupts can use them; do not copy them.
Semaphores have no ownership checks or priority inheritance, so resource
locking can suffer priority inversion. A task that cannot be signaled may
remain blocked indefinitely. Because a periodic task handles one event per
activation, choose its period and semaphore capacity to match the event rate.

## Tests

The PlatformIO Unity suites run on the Pro Micro and report results over USB
serial. With the intended board connected, run:

```powershell
pio test -e sparkfun_promicro16
```

PlatformIO uploads a separate test firmware for each suite. If multiple boards
are connected, identify the intended upload and test ports first and pass
`--upload-port` and `--test-port` as appropriate. Restore the application
firmware afterward with `pio run -e sparkfun_promicro16 -t upload`.

To compile the suites without uploading or running them:

```powershell
pio test -e sparkfun_promicro16 --without-uploading --without-testing
```

The tests disable the hardware Timer1 interrupt and invoke its vector directly
to exercise releases, preemption, priority, missed deadlines, and semaphores
deterministically. They do not verify real Timer1 timing or USB behavior.
