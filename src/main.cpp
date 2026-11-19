#include <Arduino.h>
#include "rtos.h"

#ifndef PIO_UNIT_TESTING

#define LED_GREEN 2
#define LED_RED 3

void blinkTask();
void heartbeatTask();
void workerTask();

// Tasks have fixed priorities: the first task in this table has the highest.
rtos::Task<100, 256> blink(blinkTask);
rtos::Task<1000, 256> heartbeat(heartbeatTask);
rtos::Task<2000, 256> worker(workerTask);
rtos::TaskBase* const tasks[] = {&blink, &heartbeat, &worker};

rtos::Semaphore<1,1> ledSemaphore;

void blinkTask() {
  if (ledSemaphore.tryTake()) {
    digitalWrite(LED_GREEN, !digitalRead(LED_GREEN)); // Toggle green LED
    ledSemaphore.give();
  }
}

void workerTask() {
  ledSemaphore.take();
  rtos::suspend(blink); // Suspend the blink task while doing work
  digitalWrite(LED_GREEN, LOW); // Turn off LED to indicate work
  digitalWrite(LED_RED, HIGH); // Turn on red LED to indicate work
  delay(600); // Simulate work
  digitalWrite(LED_RED, LOW); // Turn off red LED to indicate work done
  rtos::resume(blink); // Resume the blink task
  ledSemaphore.give();
}

volatile uint32_t heartbeatCount = 0;
volatile uint8_t missedDeadlines = 0;

void heartbeatTask() {
  ++heartbeatCount;
  missedDeadlines |= rtos::takeMissedDeadlines();
  Serial.print("Heartbeat: ");
  Serial.print(heartbeatCount);
  Serial.print(" Missed: ");
  Serial.println(missedDeadlines);
}

void setup() {
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  Serial.begin(115200);
  rtos::start(tasks);
}

void loop() {
  // The Arduino loop is the idle context and runs when no task is ready.
}

#endif
