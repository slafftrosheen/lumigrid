#include "board/BoardSafety.h"
#include <Arduino.h>

namespace board {

void safetyInit() {
  pinMode(25, OUTPUT);
  digitalWrite(25, HIGH);
}

void enableOutputs() {
  digitalWrite(25, LOW);
}

void disableOutputs() {
  digitalWrite(25, HIGH);
}

void safeOff() {
  disableOutputs();
}

}
