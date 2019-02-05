/*  Tidepool_heater_Sitka.ino
    Luke Miller 2019

    Designed for Revision C tidepool heater hardware, loaded with 
    Optiboot bootloader (6.2 or higher).

*/


// Include the libraries we need
#include <Wire.h>
#include <SPI.h>
//#include <OneWire.h>  // https://github.com/PaulStoffregen/OneWire
//#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
//#include <SdFat.h>  // https://github.com/greiman/SdFat
//#include "SSD1306Ascii.h" // https://github.com/greiman/SSD1306Ascii
//#include "SSD1306AsciiWire.h" // https://github.com/greiman/SSD1306Ascii
#include <INA219.h> // https://github.com/millerlp/INA219
#include <avr/wdt.h>
#include "RTClib.h"  // https://github.com/millerlp/RTClib
#include "TidelibSitkaBaronofIslandSitkaSoundAlaska.h"

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
//**********************
#define COMMON_ANODE 
#define BUTTON1 2 // Digital pin D2 (PD2) hooked to button 1
#define REDLED 9 // Red LED 
#define GRNLED 5 // Green LED
#define BLULED 6 // Blue LED
//******************************************
// Timekeeping
unsigned long myMillis = 0;
unsigned long maxHeatTime = 5 ; // time heater is allowed to run (HOURS)
unsigned long updateTime = 2; // time to update Serial monitor (seconds)
//unsigned long SDupdateTime = 10; // time to write data to SD card (seconds)
unsigned long lastTime = 0; // variable to keep previous time value
//unsigned long lastSDTime = 0; // variable to keep previous SD write time value
bool quitFlag = false; // Flag to quit the heating loop
//******************************
// Set up INA219 current/voltage monitor (default I2C address is 0x40)
Adafruit_INA219 ina219(0x40);
float shuntvoltage = 0; // Voltage drop across shunt resistor (0.01ohm on heater board)
float busvoltage = 0;  // Voltage at the load (heater)
float current_mA = 0;
float loadvoltage = 0;  // Estimated battery voltage (prior to the shunt resistor + load)
// Define a minimum safe voltage for the battery
// Target final voltage of 11.9V at battery (no load)
// With schottky diode, no load voltage drop is ~0.15V, and ~0.45V under load
// So a loadvoltage reading of 12.0 is ~12.4V at the battery under load 
// A loadvoltage reading of 11.4 under load 
// should be ~11.9 at battery with no load
float voltageMin = 11.35; // units: volts 
bool lowVoltageFlag = false;
//***********************************
// Create real time clock object
RTC_DS3231 rtc;
char buf[20]; // declare a string buffer to hold the time result
DateTime newtime;
DateTime oldtime; // used to track time in main loop
byte oldday;     // used to keep track of midnight transition
byte rtcErrorFlag = false;
//DateTime buttonTime; // hold the time since the button was pressed
//DateTime chooseTime; // hold the time stamp when a waiting period starts
volatile unsigned long buttonTime1; // hold the initial button press millis() value
byte debounceTime = 20; // milliseconds to wait for debounce
byte mediumPressTime = 2; // seconds to hold button1 to register a medium press
//***************************
// Temperature variables
//float maxTempC = 37.0; // maximum temperature (C) allowed before shutting off heater
//float warmWaterTempC = 0; // current heated water temperature
//float ambientWaterTempC = 0; // ambient water temperature

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

//*************************************************************************
void setup() {
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(REDLED, OUTPUT);
  pinMode(GRNLED, OUTPUT);
  pinMode(BLULED, OUTPUT);
  digitalWrite(REDLED, HIGH); // for common anode LED, set high to shut off
  digitalWrite(GRNLED, HIGH);
  digitalWrite(BLULED, HIGH);
  // Flash green LED to denote startup
  setColor(0, 127, 0);
  delay(100);
  setColor(0,0,0);
  // Start serial port
  Serial.begin(57600);
  Serial.println(F("Hello"));
  //*************************
  // Initialize the real time clock DS3231M
  Wire.begin(); // Start the I2C library with default options
  rtc.begin();  // Start the rtc object with default options
  newtime = rtc.now(); // read a time from the real time clock
  newtime.toString(buf, 20); 
  //***********************************************
  // Check that real time clock has a reasonable time value
  if ( (newtime.year() < 2019) | (newtime.year() > 2035) ) {
    // There is an error with the clock, halt everything
    Serial.println(F("Clock error, reprogram real time clock"));

    rtcErrorFlag = true;
    while(rtcErrorFlag){
    // Flash the error led to notify the user
    // This permanently halts execution, no data will be collected
      for (int i = 0; i < 3; i++){
        setColor(60,60,0);
        delay(50);
        setColor(0,0,0);
        delay(50);
      }
      delay(500);              
    } // end of while(rtcErrorFlag)
  } else {
    Serial.println(F("Clock okay"));
    Serial.println(buf);
  } // end of if ( (newtime.year() < 2019) | (newtime.year() > 2035) )

  //*******************************
  // Initialize the INA219.
  // By default the initialization will use the range (32V, 2A).  However
  // you can call a setCalibration function to change this range (see comments).
  ina219.begin();
  // To use a 32V, 32A range (lower precision on amps):
  ina219.setCalibration_32V_32A();
  //********************************
  shuntvoltage = ina219.getShuntVoltage_mV();

  loadvoltage = busvoltage + (shuntvoltage / 1000);
  // Enable the watchdog timer so that reset happens if anything stalls
  wdt_enable(WDTO_8S); // Enable 4 or 8 second watchdog timer timeout

  attachInterrupt(0, buttonFunc, LOW);  // BUTTON1 interrupt
  buttonFlag = false;
  newtime = rtc.now(); // get time
  oldtime = newtime;    // store time
  oldday = oldtime.day(); // Store current day value
  debounceState = DEBOUNCE_STATE_IDLE;
  mainState = STATE_IDLE; // Start the main loop in idle mode (mosfet off)
} // End of setup loop


//*************************************************************************
//*************************************************************************
void loop() {
  // Always start the loop by checking the time
  newtime = rtc.now(); // Grab the current time 
  // Also reset the watchdog timer every time the loop loops
  wdt_reset(); 
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
          if (millis() > buttonTime1 + debounceTime) {
            // If the button has been held long enough to 
            // be a legit button press, switch to 
            // DEBOUNCE_STATE_TIME to keep track of how long 
            // the button is held
            debounceState = DEBOUNCE_STATE_TIME;
            buttonTime = rtc.now();
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

        DateTime checkTime = rtc.now(); // get the time
        
        if (checkTime.unixtime() < (buttonTime.unixtime() + mediumPressTime)) {
          Serial.println(F("Short press registered"));
          // User held button briefly, treat as a normal
          // button press, which will be handled differently
          // depending on which mainState the program is in.
          buttonFlag = true;
        } else if ( (checkTime.unixtime() > (buttonTime.unixtime() + mediumPressTime)){
          // User held button1 long enough to enter mediumPressTime mode
          // Set state to STATE_ENTER_CALIB
          mainState = STATE_ENTER_CALIB;
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

  //-------------------------------------------------------------
  //-------------------------------------------------------------
  switch(mainState){
      /*  To do:
       *   Check if it's a new minute, if so re-calculate tide
       *   STATE_HEATING: If heater is on (STATE_HEATING), recalculate 
       *   average current and voltage to calculate power output, 
       *   adjust PWM signal to MOSFET. Also check that battery voltage 
       *   is above voltageMin
       *   
       *   STATE_IDLE: If heater is off (STATE_IDLE), check if conditions 
       *   are met to turn on heater (daytime, sufficient battery voltage, 
       *   predicted tide height below threshold height), and change state 
       *   to STATE_HEATING, else remain in STATE_IDLE
       *   
       *   STATE_OFF: If lowVoltageFlag is set, batteries need replacing, go into 
       *   low power mode, pulse LED to notify user. 
       * 
       */
    
    case STATE_RUN:


    break; // end of STATE_RUN case
  }

  
} // End of main loop

//--------------- buttonFunc --------------------------------------------------
// buttonFunc
void buttonFunc(void){
  detachInterrupt(0); // Turn off the interrupt
  buttonTime1 = millis();
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
