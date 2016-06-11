/* mosfet_test1.ino
 *  A simple sketch with arduino pro mini 3v3
 *  controlling pin 8 atached to gate pin 
 *  of a 30V N-channel MOSFET. The MOSFET
 *  source pin is connected to ground and 
 *  pulled down with a 10k ohm resistor to
 *  ensure it starts out "off". 
 *  Pull pin 8 high to enable the MOSFET and
 *  send power to the downstream heater. 
 */

#define MOSFET 8

void setup() {
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);

  Serial.begin(57600);
  Serial.println(F("Hello"));

  delay(2000);
  Serial.println(F("Starting heater"));
  delay(250);
  digitalWrite(MOSFET, HIGH); // turn on
  delay(3000);
  
  digitalWrite(MOSFET, LOW); // turn off
  Serial.println(F("Shutting off heat"));
  

}

void loop() {
  // Make sure the mosfet is shut off.
  digitalWrite(MOSFET, LOW);

}
