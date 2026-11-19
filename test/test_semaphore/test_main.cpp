#include <avr/interrupt.h>
#include <avr/io.h>
#include <Arduino.h>
#include <stdint.h>
#include <unity.h>

#include "rtos.h"

static void tick() {
  asm volatile("cli\n\tcall __vector_17" ::: "memory");
}

rtos::Semaphore<2> semaphore;
rtos::Semaphore<1, 1> available;
volatile uint8_t events[16];
volatile uint8_t eventCount;
volatile uint8_t lowRuns;
volatile uint16_t result;

void highTask() {
  if (lowRuns == 0 && !available.take()) result |= 1;
  if (!semaphore.take()) result |= 1;
  events[eventCount++] = lowRuns == 1 ? 2 : lowRuns == 2 ? 7 : lowRuns == 3 ? 10 : 13;
}

void middleTask() {
  if (!semaphore.take()) result |= 1;
  events[eventCount++] = lowRuns == 1 ? 4 : lowRuns == 2 ? 8 : lowRuns == 3 ? 11 : 14;
}

void lowTask() {
  ++lowRuns;
  if (lowRuns == 1) {
    events[eventCount++] = 1;
    if (!semaphore.give()) result |= 2;
    events[eventCount++] = 3;
    if (!semaphore.give()) result |= 2;
    events[eventCount++] = 5;
  } else if (lowRuns == 2) {
    events[eventCount++] = 6;
    cli();
    if (!semaphore.giveFromISR() || !semaphore.giveFromISR()) result |= 4;
    if ((SREG & _BV(SREG_I)) || eventCount != 6) result |= 8;
    sei();
    tick();
    events[eventCount++] = 9;
  } else if (lowRuns == 3) {
    tick();
    tick();
    if (rtos::takeMissedDeadlines() != 7) result |= 16;
    if (!semaphore.give() || !semaphore.give()) result |= 32;
  } else {
    events[eventCount++] = 12;
  }
}

rtos::Task<2, 256> high(highTask);
rtos::Task<2, 256> middle(middleTask);
rtos::Task<2, 256> low(lowTask);
rtos::TaskBase* tasks[3];

void test_counting_and_invalid_contexts() {
  TEST_ASSERT_FALSE(semaphore.take());
  TEST_ASSERT_FALSE(semaphore.tryTake());
  TEST_ASSERT_TRUE(semaphore.give());
  TEST_ASSERT_TRUE(semaphore.give());
  TEST_ASSERT_FALSE(semaphore.give());
  TEST_ASSERT_TRUE(semaphore.tryTake());
  TEST_ASSERT_TRUE(semaphore.tryTake());
  TEST_ASSERT_FALSE(semaphore.tryTake());

  cli();
  const bool invalidGive = semaphore.give();
  const bool invalidTake = semaphore.take();
  const bool firstGive = semaphore.giveFromISR();
  const bool secondGive = semaphore.giveFromISR();
  const bool fullGive = semaphore.giveFromISR();
  const bool firstTake = semaphore.tryTake();
  const bool secondTake = semaphore.tryTake();
  const bool interruptsEnabled = SREG & _BV(SREG_I);
  sei();

  TEST_ASSERT_FALSE(invalidGive);
  TEST_ASSERT_FALSE(invalidTake);
  TEST_ASSERT_TRUE(firstGive);
  TEST_ASSERT_TRUE(secondGive);
  TEST_ASSERT_FALSE(fullGive);
  TEST_ASSERT_TRUE(firstTake);
  TEST_ASSERT_TRUE(secondTake);
  TEST_ASSERT_FALSE(interruptsEnabled);
}

void test_blocking_priority_and_interrupt_signals() {
  tasks[0] = &high;
  tasks[1] = &middle;
  tasks[2] = &low;
  cli();
  rtos::start(tasks);
  TIMSK1 = 0;
  sei();
  TEST_ASSERT_FALSE(semaphore.take());
  tick();
  tick();
  tick();
  tick();
  tick();

  TEST_ASSERT_EQUAL_UINT16(0, result);
  TEST_ASSERT_EQUAL_UINT8(11, eventCount);
  for (uint8_t i = 0; i < 11; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i + 1, events[i]);
  }
  TEST_ASSERT_FALSE(semaphore.tryTake());
  TEST_ASSERT_FALSE(available.tryTake());
  tick();
  tick();
  TEST_ASSERT_EQUAL_UINT8(12, eventCount);
  TEST_ASSERT_TRUE(semaphore.give());
  TEST_ASSERT_EQUAL_UINT8(13, eventCount);
  TEST_ASSERT_TRUE(semaphore.give());
  TEST_ASSERT_EQUAL_UINT8(14, eventCount);
  TEST_ASSERT_EQUAL_UINT8(12, events[11]);
  TEST_ASSERT_EQUAL_UINT8(13, events[12]);
  TEST_ASSERT_EQUAL_UINT8(14, events[13]);
  TEST_ASSERT_EQUAL_UINT16(0, result);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_counting_and_invalid_contexts);
  RUN_TEST(test_blocking_priority_and_interrupt_signals);
  UNITY_END();
}

void loop() {
}
