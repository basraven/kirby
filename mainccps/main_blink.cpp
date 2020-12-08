/*
 * Blink
 * Turns on an LED on for one second,
 * then off for one second, repeatedly.
 */

#include "Arduino.h"

int LEDGPIO = 0;
void setup(){
  Serial.begin(115200);
  Serial.println("Booting");
  // initialize LED digital pin as an output.
  pinMode(LEDGPIO, OUTPUT);
  digitalWrite(LEDGPIO, LOW);
}

void loop(){
  Serial.println("Loop start");
  // turn the LED on (HIGH is the voltage level)
  digitalWrite(LEDGPIO, HIGH);
  // wait for a second
  delay(1000);
  // turn the LED off by making the voltage LOW
  digitalWrite(LEDGPIO, LOW);
   // wait for a second
  delay(1000);
}
