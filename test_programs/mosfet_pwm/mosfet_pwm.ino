/*  mosfet_pwm
 *   Luke Miller Feb 2019
 *   
 *   Test sketch to regulate power output from heater by 
 *   pulse-width-modulating the mosfet that controls the
 *   heater. Implements a moving average to deal with the
 *   fluctuating current/voltage signals that will 
 *   emanate from the INA219 current monitor due to the 
 *   pulse width modulated signal. 
 *   
 *   For use with Tidepool_heater_RevC hardware
 * 
 */
 
#include "SSD1306Ascii.h" // https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiWire.h" // https://github.com/greiman/SSD1306Ascii
#include <INA219.h> // https://github.com/millerlp/INA219
#include <avr/wdt.h>
//*************************
#define REVC  // Comment this line out to use Rev A/B hardware

// Change pin assignments based on hardware Revision
#ifdef REVC
#define ONE_WIRE_BUS 8  // For Rev C hardware
#define MOSFET 3  // Arduino digital pin 3 used to turn on MOSFET on RevC 
#else
#define ONE_WIRE_BUS 3  // For Rev A/B hardware
#define MOSFET 8  // Arduino digital pin 8 used to turn on MOSFET on RevA/B
#endif
#define BUTTON1 2 // Digital pin D2 (PD2) hooked to button 1
//**********************
#define COMMON_ANODE 
#define REDLED 9 // Red LED 
#define GRNLED 5 // Green LED
#define BLULED 6 // Blue LED
//******************************************
// Define the OLED display object
SSD1306AsciiWire oled;  // When using Wire library
#define I2C_ADDRESS 0x3C
//******************************************
// Timekeeping
unsigned long myMillis = 0;
int reportTime = 2000; // milliseconds between serial printouts
volatile unsigned long button1Time; // hold the initial button press millis() value
byte debounceTime = 20; // milliseconds to wait for debounce
byte mediumPressTime = 2000; // milliseconds to hold button1 to register a medium press
volatile byte buttonFlag = false;
//******************************
// Set up INA219 current/voltage monitor (default I2C address is 0x40)
Adafruit_INA219 ina219(0x40);
float currentShuntVoltage = 0; // Voltage drop across shunt resistor (0.01ohm on heater board)
float currentBusVoltage = 0;  // Voltage at the load (heater)
float currentCurrentValue = 0; // Current in mA
float loadVoltage = 0;  // Estimated battery voltage (prior to the shunt resistor + load)
float Watts = 0; // Estimate power output, Watts
// Define a minimum safe voltage for the battery
// Target final voltage of 11.9V at battery (no load)
// With schottky diode, no load voltage drop is ~0.15V, and ~0.45V under load
// So a loadvoltage reading of 12.0 is ~12.4V at the battery under load 
// A loadvoltage reading of 11.4 under load 
// should be ~11.9 at battery with no load
float voltageMin = 11.35; // units: volts 
bool lowVoltageFlag = false;
float maxWatts = 31;
float minWatts = 28; 
// Variables for the Modified Moving Average
float movingAverageCurr = 0;
float movingAverageCurrSum = 0;
float movingAverageBusV = 0;
float movingAverageBusVSum = 0;
float movingAverageShuntV = 0;
float movingAverageShuntVSum = 0;
// Number of samples for moving average:
const byte averageCount = 100;
word myPWM = 0; // 0-255 pulse width modulation value for MOSFET
word maxPWM = 127; // 0-255
byte flashFlag = false; // Used to flash LED

// ***** TYPE DEFINITIONS *****
typedef enum STATE
{
  STATE_IDLE, // Waiting for conditions to trigger heating
  STATE_HEATING, // Actively heating and monitoring battery voltage
  STATE_OFF, // Battery low, wait for user to replace batteries
} mainState_t;

typedef enum DEBOUNCE_STATE
{
  DEBOUNCE_STATE_IDLE,
  DEBOUNCE_STATE_CHECK,
  DEBOUNCE_STATE_TIME
} debounceState_t;

// main state machine variable, this takes on the various
// values defined for the STATE typedef above. 
mainState_t mainState;

// debounce state machine variable, this takes on the various
// values defined for the DEBOUNCE_STATE typedef above.
volatile debounceState_t debounceState;


//***************************************************
void setup() {
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW); // pull low to turn off mosfet
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(REDLED, OUTPUT);
  pinMode(GRNLED, OUTPUT);
  pinMode(BLULED, OUTPUT);
  // Flash green LED to denote startup
  setColor(0, 127, 0);
  delay(100);
  setColor(0,0,0);
  Serial.begin(57600);
  Serial.println(F("Hello"));
  // ************************************
  // Initialize the OLED display
  oled.begin(&Adafruit128x64, I2C_ADDRESS);  // For 128x64 OLED screen
  oled.setFont(Adafruit5x7);
  oled.clear();  
  //*******************************
  // Initialize the INA219.
  // By default the initialization will use the range (32V, 2A).  However
  // you can call a setCalibration function to change this range (see comments).
  ina219.begin();
  // To use a 32V, 32A range (lower precision on amps):
  ina219.setCalibration_32V_32A();
  // Enable the watchdog timer so that reset happens if anything stalls
  wdt_enable(WDTO_8S); // Enable 4 or 8 second watchdog timer timeout

  attachInterrupt(0, buttonFunc, LOW);  // BUTTON1 interrupt
  buttonFlag = false;
  debounceState = DEBOUNCE_STATE_IDLE;
  mainState = STATE_IDLE; // Start the main loop in idle mode (mosfet off)
  // Initialize the moving average current monitoring
  for (int x=0; x < averageCount; x++){
      currentCurrentValue = ina219.getCurrent_mA();
      currentBusVoltage = ina219.getBusVoltage_V();
      currentShuntVoltage = ina219.getShuntVoltage_mV();
      movingAverageCurrSum += currentCurrentValue; // add in 1 new value
      movingAverageBusVSum += currentBusVoltage;
      movingAverageShuntVSum += currentShuntVoltage;
      delay(2);
  }
  movingAverageCurr = movingAverageCurrSum / averageCount; // Calculate average
  movingAverageBusV = movingAverageBusVSum/ averageCount; // Calculate average
  movingAverageShuntV = movingAverageShuntVSum / averageCount; // Calculate average
  loadVoltage = movingAverageBusV + (movingAverageShuntV / 1000); // Average battery voltage
  Watts = loadVoltage * movingAverageCurr; // Calculate estimated power output, Watts
  myMillis = millis(); // Initialize
  
} // end of setup loop


//**************************************************************
void loop() {
  // Reset the watchdog timer every time the main loop loops
  wdt_reset(); 
  // Report current current flow value
  if ( millis() > (myMillis + reportTime) ){
      Serial.print(F("Average current: "));
      Serial.print(movingAverageCurr);
      Serial.print(F("mA\t Voltage:"));
      Serial.print(loadVoltage);
      Serial.print(F("V\t PWM setting: "));
      Serial.print(myPWM);
      Serial.print(F("\t"));
      if (mainState == STATE_HEATING){
        Serial.println("Heating");
      } else if (mainState == STATE_IDLE){
        Serial.println("Idle");
      }
      PrintOLED();
      myMillis = millis(); // update myMillis
      flashFlag = !flashFlag;
      if (mainState == STATE_IDLE & flashFlag) {
        // Turn on green LED
        setColor(0,127,0);
      } else if (mainState == STATE_IDLE & !flashFlag){
        // Turn off green LED
        setColor(0,0,0);
      }
  }

  //-------------------------------------------------------------
  // Begin loop by checking the debounceState to 
  // handle any button presses
  switch (debounceState) {
    // debounceState should normally start off as 
    // DEBOUNCE_STATE_IDLE until button1 is pressed,
    // which causes the state to be set to 
    // DEBOUNCE_STATE_CHECK
    //************************************
    case DEBOUNCE_STATE_IDLE:
      // Do nothing in this case
    break;
    //************************************
    case DEBOUNCE_STATE_CHECK:
      // If the debounce state has been set to 
      // DEBOUNCE_STATE_CHECK by the buttonFunc interrupt,
      // check if the button is still pressed
      if (digitalRead(BUTTON1) == LOW) {
          if (millis() > button1Time + debounceTime) {
            // If the button has been held long enough to 
            // be a legit button press, switch to 
            // DEBOUNCE_STATE_TIME to keep track of how long 
            // the button is held
            debounceState = DEBOUNCE_STATE_TIME;
            button1Time = millis();
          } else {
            // If button is still pressed, but the debounce 
            // time hasn't elapsed, remain in this state
            debounceState = DEBOUNCE_STATE_CHECK;
          }
        } else {
          // If button1 is high again when we hit this
          // case in DEBOUNCE_STATE_CHECK, it was a false trigger
          // Reset the debounceState
          debounceState = DEBOUNCE_STATE_IDLE;
          buttonFlag = false;
          // Restart the button1 interrupt
          attachInterrupt(0, buttonFunc, LOW);
        }
    break; // end of case DEBOUNCE_STATE_CHECK
    //*************************************
    case DEBOUNCE_STATE_TIME:
      if (digitalRead(BUTTON1) == HIGH) {
        // If the user released the button, now check how
        // long the button was depressed. This will determine
        // which state the user wants to enter. 
        unsigned long checkTime = millis(); // get the time
        
        if (checkTime < (button1Time + mediumPressTime)) {
          Serial.println(F("Short press registered"));
          // User held button briefly, treat as a normal
          // button press, which will be handled differently
          // depending on which mainState the program is in.
          // For a short press, enter IDLE mode
          mainState = STATE_IDLE;
          buttonFlag = true;
        } else if ( checkTime > (button1Time + mediumPressTime)) {
          // User held button1 long enough to enter mediumPressTime mode
          // Set main state to HEATING for a long-press
          mainState = STATE_HEATING;
          buttonFlag = true;
        }
        // Now that the button press has been handled, return
        // to DEBOUNCE_STATE_IDLE and await the next button press
        debounceState = DEBOUNCE_STATE_IDLE;
        // Restart the button1 interrupt now that button1
        // has been released
        attachInterrupt(0, buttonFunc, LOW);  
      } else {
        // If button is still low (depressed), remain in 
        // this DEBOUNCE_STATE_TIME
        debounceState = DEBOUNCE_STATE_TIME;
      }
    break; // end of case DEBOUNCE_STATE_TIME 
  } // end switch(debounceState)
  //************************************************

  //-------------------------------------------------------------
  switch(mainState){
    case STATE_IDLE:
      digitalWrite(MOSFET, LOW); // make sure mosfet is off
      PowerSample(ina219); // Sample INA219 and update current,voltage,power variables
    break; // end of STATE_IDLE case

    case STATE_HEATING:
      analogWrite(MOSFET, myPWM);
      setColor(127,0,0); // red LED on
      PowerSample(ina219); // Sample the INA219 and update current,voltage,power variables
      if (loadVoltage > voltageMin) {
        // If the loadVoltage is still above voltageMin, then continue
        // heating
        if (Watts > minWatts & Watts < maxWatts){
          // Wattage is within desired range, do nothing
        } else if (Watts < minWatts) {
          // Wattage is low, adjust PWM value up
          if (myPWM < maxPWM) {
            myPWM += 1;
          }
        } else if (Watts > maxWatts) {
          // Wattage is high, lower PWM value
          if (myPWM > 0) {
            myPWM -= 1;
          }
        }         
      } else {
        // if loadVoltage less than voltageMin, shut off the heater to 
        // preserve the battery.
        mainState = STATE_IDLE;
      }
    break; // end of STATE_RUN case
 
  } // end of mainState switch statement
} // end of main loop


//--------------- buttonFunc --------------------------------------------------
// buttonFunc
void buttonFunc(void){
  detachInterrupt(0); // Turn off the interrupt
  button1Time = millis();
  debounceState = DEBOUNCE_STATE_CHECK; // Switch to new debounce state
}
//-----------------setColor---------------------
// Enter a set of values 0-255 for the red, green, and blue LED channels
void setColor(int red, int green, int blue)
{
  #ifdef COMMON_ANODE
    red = 255 - red;
    green = 255 - green;
    blue = 255 - blue;
  #endif
  analogWrite(REDLED, red);
  analogWrite(GRNLED, green);
  analogWrite(BLULED, blue);  
}

//---------------PowerSample-----------------------------
// Function to sample the INA219 current monitor, update
// the moving average values, and update the average
// current, system voltage, and power output. This assumes
// a lot of global variables
void PowerSample(Adafruit_INA219& ina219) {
      currentCurrentValue = ina219.getCurrent_mA();
      currentBusVoltage = ina219.getBusVoltage_V();
      currentShuntVoltage = ina219.getShuntVoltage_mV();
      // Update average current 
      movingAverageCurrSum -= movingAverageCurr; // remove 1 value
      movingAverageCurrSum += currentCurrentValue; // add in 1 new value
      movingAverageCurr = movingAverageCurrSum / averageCount; // recalculate average
      // Update average bus voltage
      movingAverageBusVSum -= movingAverageBusV;
      movingAverageBusVSum += currentBusVoltage;
      movingAverageBusV = movingAverageBusVSum / averageCount;
      // Update average shunt voltage
      movingAverageShuntVSum -= movingAverageShuntV;
      movingAverageShuntVSum += currentShuntVoltage;
      movingAverageShuntV = movingAverageShuntVSum / averageCount;
      // Update average load voltage
      loadVoltage = movingAverageBusV + (movingAverageShuntV / 1000);
      // Update power output
      Watts = loadVoltage * movingAverageCurr;
}

//----------PrintoledTemps------------------
/* Function to print current + voltage
 *  to the OLED display.
 *  After calling this function send another
 *  oled.println() call if you need to write
 *  a 4th line of info
 */
void PrintOLED(void)
{
  oled.clear();
  oled.print(F("Status: "));
  if (mainState == STATE_IDLE){
    oled.print(F("IDLE"));
  } else if (mainState == STATE_HEATING){
    oled.print(F("HEATING"));
  }
  oled.println();
  // 2nd line
  oled.print(F("Volts: "));
  oled.println(loadVoltage);
  // 3rd line, show battery current
  oled.print(F("mA: "));
  oled.println(movingAverageCurr);
  // 4th line, show Wattage
  oled.print(F("Watts: "));
  oled.println(Watts);
}
