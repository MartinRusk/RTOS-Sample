#include <avr/interrupt.h>
#include <avr/io.h>
#include <Arduino.h>
#include <stdint.h>
#include <unity.h>

#include "rtos.h"

// The instruction simulator does not generate Timer1 interrupts. Calling the
// vector with interrupts disabled gives it the same return frame as an IRQ.
static void tick() {
  asm volatile("cli\n\tcall __vector_17" ::: "memory");
}

volatile uint8_t events[12];
volatile uint8_t eventCount;
volatile uint8_t aCount;
volatile uint8_t bCount;
volatile uint8_t observedMisses;

void taskA() {
  ++aCount;
  if (aCount == 1) {
    events[eventCount++] = 1;
    tick();  // Release B while A is running.
    events[eventCount++] = 2;
  } else if (aCount == 2) {
    events[eventCount++] = 4;
    tick();
    tick();  // Both active tasks miss a release.
  } else {
    events[eventCount++] = 6;
  }
}

void taskB() {
  ++bCount;
  if (bCount == 1) {
    events[eventCount++] = 3;
    tick();  // Release A and preempt B.
    events[eventCount++] = 5;
    observedMisses = rtos::takeMissedDeadlines();
  } else {
    events[eventCount++] = 7;
  }
}

rtos::Task<2, 256> a(taskA);
rtos::Task<3, 256> b(taskB);
rtos::TaskBase* tasks[2];

volatile uint8_t suspendedTaskRuns;
volatile uint8_t peerTaskRuns;

void suspendedTaskFunction() {
  ++suspendedTaskRuns;
}

void peerTaskFunction() {
  ++peerTaskRuns;
}

rtos::Task<1, 256> suspendedTask(suspendedTaskFunction);
rtos::Task<1, 256> peerTask(peerTaskFunction);
rtos::TaskBase* suspensionTasks[] = {&suspendedTask, &peerTask};
rtos::Task<1, 256> unregisteredTask(suspendedTaskFunction);

void test_periodic_scheduling_and_deadlines() {
  tasks[0] = &a;
  tasks[1] = &b;
  cli();
  rtos::start(tasks);
  TIMSK1 = 0;
  sei();

  tick();
  TEST_ASSERT_EQUAL_UINT8(0, eventCount);
  tick();
  TEST_ASSERT_EQUAL_UINT8(5, eventCount);
  TEST_ASSERT_EQUAL_UINT8(3, observedMisses);
  TEST_ASSERT_EQUAL_UINT8(0, rtos::takeMissedDeadlines());
  for (uint8_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i + 1, events[i]);
  }

  tick();
  tick();
  tick();
  TEST_ASSERT_EQUAL_UINT8(7, eventCount);
  TEST_ASSERT_EQUAL_UINT8(6, events[5]);
  TEST_ASSERT_EQUAL_UINT8(7, events[6]);
  tick();
  tick();
  tick();  // Both tasks are released; A must run before B.
  TEST_ASSERT_EQUAL_UINT8(10, eventCount);
  TEST_ASSERT_EQUAL_UINT8(6, events[7]);
  TEST_ASSERT_EQUAL_UINT8(6, events[8]);
  TEST_ASSERT_EQUAL_UINT8(7, events[9]);
}

void test_task_suspend_and_resume() {
  suspendedTaskRuns = 0;
  peerTaskRuns = 0;
  cli();
  rtos::start(suspensionTasks);
  TIMSK1 = 0;
  sei();

  tick();
  TEST_ASSERT_EQUAL_UINT8(1, suspendedTaskRuns);
  TEST_ASSERT_EQUAL_UINT8(1, peerTaskRuns);

  TEST_ASSERT_TRUE(rtos::suspend(suspendedTask));
  tick();
  TEST_ASSERT_EQUAL_UINT8(1, suspendedTaskRuns);
  TEST_ASSERT_EQUAL_UINT8(2, peerTaskRuns);

  TEST_ASSERT_TRUE(rtos::resume(suspendedTask));
  TEST_ASSERT_EQUAL_UINT8(2, suspendedTaskRuns);
  TEST_ASSERT_FALSE(rtos::suspend(unregisteredTask));

  cli();
  const bool invalidResume = rtos::resume(suspendedTask);
  sei();
  TEST_ASSERT_FALSE(invalidResume);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_periodic_scheduling_and_deadlines);
  RUN_TEST(test_task_suspend_and_resume);
  UNITY_END();
}

void loop() {
}
