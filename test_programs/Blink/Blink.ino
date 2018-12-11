// Blink.ino
/* Basic test of the RGB LED on Tidepool_heater_RevA 
 *  hardware. Uses a common anode led APFA3010SEEZGKQBKC
 *  from Kingbright.
 *  
 *  
 */

int REDLED = 9; // PB1
int GRNLED = 5; // PD5
int BLUELED = 6; // PD6

#define COMMON_ANODE

void setup() {
  pinMode(REDLED, OUTPUT);
  pinMode(GRNLED, OUTPUT);
  pinMode(BLUELED, OUTPUT);  
}

void loop() {
  setColor(255, 0, 0);  // red
  delay(1000);
  setColor(0, 255, 0);  // green
  delay(1000);
  setColor(0, 0, 255);  // blue
  delay(1000);
  setColor(230, 255, 0);  // yellow
  delay(1000);  
  setColor(80, 0, 80);  // purple
  delay(1000);
  setColor(0, 255, 240);  // aqua
  delay(1000);
}

void setColor(int red, int green, int blue)
{
  #ifdef COMMON_ANODE
    red = 255 - red;
    green = 255 - green;
    blue = 255 - blue;
  #endif
  analogWrite(REDLED, red);
  analogWrite(GRNLED, green);
  analogWrite(BLUELED, blue);  
}
