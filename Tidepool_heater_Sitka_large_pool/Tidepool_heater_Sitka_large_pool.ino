/*  Tidepool_heater_Sitka_large_pool.ino
    Luke Miller 2019
    Heater output set to 30W for larger tide pools (11, 13, 16, 26, 33, 36)
    Tide predictions for Sitka Alaska, assuming UTC-9 time zone year around
    Make sure onboard clock is set to UTC-9 time zone, ignore daylight savings time

    See the Customization Variables section below for user-adjustable variables
    
    Status LED codes:
    Green flash = idle, waiting for proper conditions to begin heating
    Red flash = active heating
    Blue flash = battery voltage low, replace batteries
    Fast white flash = heater failure, reboot and check heater
    Fast red-green-blue = real time clock failure, reprogram clock

    Designed for Revision C tidepool heater hardware, loaded with 
    Optiboot bootloader (6.2 or higher).

*/


// Include the libraries we need
#include <Wire.h>
#include <SPI.h>
//#include <OneWire.h>  // https://github.com/PaulStoffregen/OneWire
//#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
//#include <SdFat.h>  // https://github.com/greiman/SdFat
#include "SSD1306Ascii.h" // https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiWire.h" // https://github.com/greiman/SSD1306Ascii
#include "INA219.h" // https://github.com/millerlp/INA219
#include <avr/wdt.h>
#include "RTClib.h"  // https://github.com/millerlp/RTClib
#include "TidelibSitkaBaronofIslandSitkaSoundAlaska.h"
//#include "LowPower.h" // https://github.com/rocketscream/Low-Power/
//***********************************************************************
//*******Customization variables*****************************************
float tideHeightThreshold = 5.9; // threshold for low vs high tide, units feet (5.9ft = 1.8m)
float maxWatts = 30.5; // max power output of heater
float minWatts = 29.5; // minimum power output of heater
long heatTimeLimit = 5; // Time limit (hours) for heating during one low tide
//***********************************************************************
//***********************************************************************
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
// Define the OLED display object
SSD1306AsciiWire oled;  // When using Wire library
#define I2C_ADDRESS 0x3C
//*********************************************
// Tide calculator setup
TideCalc myTideCalc; // Create TideCalc object called myTideCalc
float tideHeightft; // Hold results of tide calculation, units feet
//******************************************
// Timekeeping
// Create real time clock object
RTC_DS3231 rtc;
char buf[20]; // declare a string buffer to hold the time result
DateTime newtime;
DateTime oldtime; // used to track time in main loop
byte oldday;     // used to keep track of midnight transition
byte rtcErrorFlag = false;
volatile unsigned long button1Time; // hold the initial button press millis() value
byte debounceTime = 20; // milliseconds to wait for debounce
int mediumPressTime = 2000; // milliseconds to hold button1 to register a medium press
unsigned long myMillis = 0;
int reportTime = 2000; // milliseconds between serial printouts
int sunriseHour = 6; // default hour for sunrise
int sunsetHour = 20; // default hour for sunset
bool quitFlag = false; // Flag to quit the heating loop
DateTime startTime;
DateTime endTime;

bool lowtideLimitFlag = false; // Used to quit heating during one low tide
//******************************
// Set up INA219 current/voltage monitor (default I2C address is 0x40)
Adafruit_INA219 ina219(0x40);
float currentShuntVoltage = 0; // Voltage drop across shunt resistor (0.01ohm on heater board)
float currentBusVoltage = 0;  // Voltage at the load (heater)
float currentCurrentValue = 0; // Current in mA
float currentPowerValue = 0; // Power in mW
float loadVoltage = 0;  // Estimated battery voltage (prior to the shunt resistor + load)
float Watts = 0; // Estimated power output, Watts
// Define a minimum safe voltage for the battery
// Target final voltage of 11.9V at battery (no load)
// With schottky diode, no load voltage drop is ~0.15V, and ~0.45V under load
// So a loadvoltage reading of 12.0 is ~12.4V at the battery under load 
// A loadvoltage reading of 11.4 under load 
// should be ~11.9 at battery with no load
// Avoid pulling the battery below 11.75V at rest (about 11.2v under load)
float voltageMin;


bool lowVoltageFlag = false;
// Variables for the Modified Moving Average
float movingAverageCurr = 0;
float movingAverageCurrSum = 0;
float movingAverageBusV = 0;
float movingAverageBusVSum = 0;
float movingAverageShuntV = 0;
float movingAverageShuntVSum = 0;
float movingAveragePower = 0;
float movingAveragePowerSum = 0;
// Number of samples for moving average:
const byte averageCount = 100;
word myPWM = 0; // 0-255 pulse width modulation value for MOSFET
word maxPWM = 255; // 0-255, 255 is full-on, 0 is off
bool flashFlag = false; // Used to flash LED
bool heaterFailFlag = false; // Used to indicate failed heater
bool errorFlashFlag = false; // Used to toggle heater fail flashing
int failCounter = 0; // Used to keep track of how long failure has gone on

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
  delay(200);
  setColor(0,0,0);
  // Start serial port
  Serial.begin(57600);
  Serial.println(F("Hello"));
  Serial.print(F("Power setting: "));
  Serial.print(maxWatts);
  Serial.print(F("-"));
  Serial.print(minWatts);
  Serial.println(F(" Watts"));
  Serial.print(F("Tide threshold: "));
  Serial.print(tideHeightThreshold);
  Serial.println(F(" ft."));
  Serial.print(F("Heating time limit, hrs: "));
  Serial.println(heatTimeLimit);
  //*************************
  // Initialize the real time clock DS3231M
  Wire.begin(); // Start the I2C library with default options
  rtc.begin();  // Start the rtc object with default options
  printTimeSerial(rtc.now()); // print time to serial monitor
  Serial.println();
  newtime = rtc.now(); // read a time from the real time clock
  //***********************************************
  // Check that real time clock has a reasonable time value
  if (newtime.year() < 2019 | newtime.year() > 2035) {
    // There is an error with the clock, halt everything
    while(1){
    // Flash the error led to notify the user
    // This permanently halts execution, no data will be collected
      setColor(127,0,0);  // red
      delay(150);
      setColor(0,127,0); // green
      delay(150);
      setColor(0,0,127); // blue
      delay(150);
      setColor(0,0,0);  // off
      delay(150);
    } // while loop end
  }
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
  //********************************
  // Set the minimum voltage under load (heater on). It can be slightly lower
  // for high-wattage applications because the unloaded voltage tends to end
  // up rebounding a bit higher.
  if (minWatts > 25.0){
     // 30watt heaters can run to a lower minimum voltage
    voltageMin = 11.00; // units: volts   
  } else if (minWatts < 25.0) {
    // 20watt heaters need to have a higher minimum since they
    // tend t draw down the unloaded voltage lower as well due to the 
    // lower load.
    voltageMin = 11.20; // units: volts   
  }
  attachInterrupt(0, buttonFunc, LOW);  // BUTTON1 interrupt
  newtime = rtc.now(); // get time
  oldtime = newtime;    // store time
  oldday = oldtime.day(); // Store current day value
  updateSunriseSunset(newtime, 0); // Update sunrise and sunset times
  // Show user the sunrise and sunset times (these are really just 
  // time ranges during the day when we want the heater to run)
  Serial.print(F("Sunrise start hour: "));
  Serial.print(sunriseHour);
  Serial.print(F(", Sunset stop hour: "));
  Serial.println(sunsetHour);

  // Test the attached heater, current should be > 1000mA
  // Turn on heater mosfet full-on
  analogWrite(MOSFET, 255);
  // Initialize the moving average current monitoring
  for (int x=0; x < averageCount; x++){
      movingAverageCurrSum += ina219.getCurrent_mA();
      movingAverageBusVSum += ina219.getBusVoltage_V();
      movingAverageShuntVSum += ina219.getShuntVoltage_mV();
      movingAveragePowerSum += ina219.getPower_mW();
      movingAverageCurrSum += currentCurrentValue; // add in 1 new value
      delay(1);
  }
  movingAverageCurr = movingAverageCurrSum / averageCount; // Calculate average, mA
  movingAverageBusV = movingAverageBusVSum/ averageCount; // Calculate average, Volts
  movingAverageShuntV = movingAverageShuntVSum / averageCount; // Calculate average, mV
  movingAveragePower = movingAveragePowerSum / averageCount; // Calculate average, mW
  loadVoltage = movingAverageBusV + (movingAverageShuntV / 1000); // Average battery voltage
  Watts = movingAveragePower / 1000; // Convert estimated power output mW to Watts
  if (movingAverageCurr < 1000){
    heaterFailFlag = true;
    Serial.println(F("Heater failed?"));
    for (int i = 0; i<5; i++){
      setColor(255,255,255); // white color
      delay(20);
      setColor(0,0,0);
      delay(50);
    }
  }
  analogWrite(MOSFET, 0); // turn heater mosfet off
  //------------------------
  heatTimeLimit = heatTimeLimit * 60 * 60; // convert hours to seconds
  debounceState = DEBOUNCE_STATE_IDLE;
  mainState = STATE_IDLE; // Start the main loop in idle mode (mosfet off)
  // Enable the watchdog timer so that reset happens if anything stalls
  wdt_enable(WDTO_8S); // Enable 4 or 8 second watchdog timer timeout
  newtime = rtc.now();
  oldtime = newtime;
  oldday = oldtime.day();
  startTime = newtime;
  endTime = newtime;
  // Calculate new tide height based on current time
  tideHeightft = myTideCalc.currentTide(newtime);
  myMillis = millis(); // Initialize
  
} // End of setup loop


//*************************************************************************
//*************************************************************************
void loop() {
  // Reset the watchdog timer every time the loop loops
  wdt_reset();
  // Update time and check if a new minute has started
  newtime = rtc.now();
  
  if ( newtime.minute() != oldtime.minute() ) {
    // If the minute values don't match, a new minute has turned over
    // Recalculate tide height, update oldtime
    oldtime = newtime;
    // Calculate new tide height based on current time
    tideHeightft = myTideCalc.currentTide(newtime);
    // Reset lowtideLimitFlag if tide is above threshold
    if (tideHeightft > (tideHeightThreshold + 0.1)){
      lowtideLimitFlag = false; // Reset to false to allow heating on next low tide 
    }
    // Call update function to update sunrise/sunset values, if needed
    updateSunriseSunset(newtime, oldday); 
  }
  // Report current current flow value if the reportTime interval has elapsed
  if ( millis() > (myMillis + reportTime) ){
        printTimeSerial(newtime); // print time to serial monitor
        Serial.print(F(", Tide ft: "));
        Serial.println(tideHeightft);
//      Serial.print(F("Average current: "));
//      Serial.print(movingAverageCurr);
//      Serial.print(F("mA\t Voltage:"));
//      Serial.print(loadVoltage);
//      Serial.print(F("V\t PWM setting: "));
//      Serial.print(myPWM);
//      Serial.print(F("\t"));
//      if (mainState == STATE_HEATING){
//        Serial.println("Heating");
//      } else if (mainState == STATE_IDLE){
//        Serial.println("Idle");
//      }
      PrintOLED();
      myMillis = millis(); // update myMillis
      flashFlag = !flashFlag;
      if (heaterFailFlag){
        errorFlashFlag = true; // toggle error flash
      }
  } 
 
  //-------------------------------------------------------------
  // Check the debounceState to 
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
//          Serial.println(F("Short press registered"));
          // User held button briefly, treat as a normal
          // button press, which will be handled differently
          // depending on which mainState the program is in.
          mainState = STATE_IDLE;
        } else if ( checkTime > (button1Time + mediumPressTime)){
          // User held button1 long enough to enter mediumPressTime mode
          // Set state to STATE_ENTER_CALIB
          mainState = STATE_HEATING;
          startTime = rtc.now(); // record start time
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
    case STATE_IDLE:
      myPWM = 0; // Set pwm value to zero 
      analogWrite(MOSFET, myPWM); // Make sure heater is off
      PowerSample(ina219); // Update power usage values
      if (flashFlag) {
        if (heaterFailFlag & errorFlashFlag){
          for (int i = 0; i<5;i++){
            setColor(127,127,127); // white color
            delay(10);
            setColor(0,0,0);
            delay(50);
          }
        }
        // Turn on green LED
        setColor(0,127,0);
      } else if (!flashFlag){
        // Turn off green LED
        setColor(0,0,0);
      }
      // Check and see if conditions are appropriate for turning the
      // heater on. If the predicted tide height is less than the
      // tideHeightThreshold, and time is later than sunriseHour and
      // earlier than sunsetHour, and the lowVoltageflag is still false,
      // and the lowtideLimitFlag is false,
      // then the heating can begin. 
      if ( (tideHeightft < tideHeightThreshold) & 
        (newtime.hour() >= sunriseHour) &
        (newtime.hour() < sunsetHour) & !lowVoltageFlag & !lowtideLimitFlag){
          mainState = STATE_HEATING;  // Switch to heating mode
          startTime = rtc.now(); // record start time
      }
    break; // end of STATE_IDLE case
    //**********************************
    // You can arrive at STATE_HEATING via one of 2 paths, either
    // because the user pressed button1 long enough to trigger the 
    // state change, or because the heating conditions were satisfied
    // in the STATE_IDLE case. 
    case STATE_HEATING:
      // First check to see if we should still be heating, or if the tide
      // has risen past the height threshold, or if we've been heating for 
      // longer than heatTimeLimit, or if the current time is later than sunsetHour
      if ( (tideHeightft < tideHeightThreshold) & 
        ( (newtime.unixtime() - startTime.unixtime() ) < heatTimeLimit) |
        newtime.hour() >= sunsetHour) {
          // If those tests are passed, then continue heating
          analogWrite(MOSFET, myPWM);
          endTime = newtime;
          PowerSample(ina219); // Sample INA219 and update current,voltage,power variables
          if (flashFlag) {
            // Turn on red LED
            setColor(127,0,0);
          } else if (!flashFlag){
            // Turn off red LED
            setColor(0,0,0);
          }
          // If heater power is on, make sure voltageMin hasn't been passed
          if (loadVoltage > voltageMin) {
            // If the loadVoltage is still above voltageMin, then continue
            // heating, adjust power output if necessary
            if (Watts > minWatts & Watts < maxWatts){
              // Wattage is within desired range, do nothing
            } else if (Watts < minWatts) {
              // Wattage is low, adjust PWM value up
              if (myPWM < maxPWM) {
                myPWM += 1;
              }
              if (myPWM == 255 & Watts < 1.0){
                // If the heater mosfet is fully on (255) and power usage
                // is still less than 1 Watt, the heater is probably broken
                // Increment the failure counter
                failCounter++;
                if (failCounter > 2000){
                  // If enough failure cycles occur, shut it down
                  heaterFailFlag = true; // set the failure flag
                  mainState = STATE_OFF; // shut it down
                } 
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
            mainState = STATE_OFF;
            lowVoltageFlag = true; // Set the lowVoltageFlag true to avoid further heating
            Serial.println(F("Low battery voltage"));
            endTime = rtc.now(); // record finishing time
          }

        } else if (tideHeightft >= tideHeightThreshold) {
          // Tide is high, go back to idle state
          mainState = STATE_IDLE;
          lowtideLimitFlag = false;
        } else if ( newtime.unixtime() - startTime.unixtime() >= heatTimeLimit) {
          // Been heating for more than heatTimeLimit, so set lowtideLimitFlag to true
          lowtideLimitFlag = true;
          mainState = STATE_IDLE;
        } else if ( newtime.hour() >= sunsetHour) {
          // If the time has rolled past sunsetHour, turn off
          mainState = STATE_IDLE;
          lowtideLimitFlag = false;
        }
      
    break; // end of STATE_HEATING case
    //***********************************************
    // STATE_OFF handles the case when the lowVoltageFlag is true,
    // and forces the device into a waiting mode that it can't escape
    // from without user intervention (reset or button press).
    case STATE_OFF:
      wdt_disable(); // Turn off the watchdog timer that was running
      myPWM = 0; // Set pwm value to zero 
      analogWrite(MOSFET, myPWM); // Make sure heater is off
      PowerSample(ina219); // Sample INA219 and update current,voltage,power variables
      if (flashFlag){
        if (heaterFailFlag & errorFlashFlag){
          for (byte i = 0; i < 5 ; i++){
            setColor(127,127,127); // flash white quickly
            delay(10);
            setColor(0,0,0);
            delay(50);
          }
          errorFlashFlag = false; // toggle error flash off
        }
        if (lowVoltageFlag){
         setColor(0,0,60); // flash blue color  
        }
        
      } else if (!flashFlag) {
        setColor(0,0,0); // turn off LED
      }

      mainState = STATE_OFF;
    break;
  }

  
} // End of main loop

//----------------Functions----------------------------------------------------

//-----------------------------------------------------------------------------
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
      currentPowerValue = ina219.getPower_mW();
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
      movingAveragePowerSum -= movingAveragePower;
      movingAveragePowerSum += currentPowerValue;
      movingAveragePower = movingAveragePowerSum / averageCount;
      Watts = movingAveragePower / 1000; // Convert mW to Watts
}


//----------PrintOLED------------------
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
  } else if (mainState == STATE_OFF){
    if (lowVoltageFlag & !heaterFailFlag){
      oled.print(F("Replace battery"));  
    } else if (heaterFailFlag & !lowVoltageFlag){
      oled.print(F("Heater failed"));
    } else if (lowVoltageFlag & heaterFailFlag){
      // both failed
      if (errorFlashFlag){
        oled.print(F("Heater failed"));  
      } else {
        oled.print(F("Replace batt"));
      }
    }
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
  oled.print(Watts);
  oled.print(F(" PWM: "));
  oled.println(myPWM);
  // 5th line
  oled.print(F("Tide ft:"));
  oled.println(tideHeightft);
  // 6th line
  printTimeOLED(newtime);
  // 7th line, run time
//  if (mainState == STATE_OFF){
    oled.println();
    oled.print(F("Run time, mins: "));
    oled.print( (endTime.unixtime() - startTime.unixtime()) / 60);
//  }
  // Fail flags
  if (heaterFailFlag){
    oled.println();
    oled.print(F("Heater fail"));
  }
}

//-----------printTimeOLED------------------------
void printTimeOLED(DateTime now){
//------------------------------------------------
// printTimeOLED function takes a DateTime object from
// the real time clock and prints the date and time 
// to the OLED display. 
  oled.print(now.year(), DEC);
    oled.print('-');
  if (now.month() < 10) {
    oled.print(F("0"));
  }
    oled.print(now.month(), DEC);
    oled.print('-');
    if (now.day() < 10) {
    oled.print(F("0"));
  }
  oled.print(now.day(), DEC);
    oled.print(' ');
  if (now.hour() < 10){
    oled.print(F("0"));
  }
    oled.print(now.hour(), DEC);
    oled.print(':');
  if (now.minute() < 10) {
    oled.print("0");
  }
    oled.print(now.minute(), DEC);
    oled.print(':');
  if (now.second() < 10) {
    oled.print(F("0"));
  }
    oled.print(now.second(), DEC);
  // You may want to print a newline character
  // after calling this function i.e. oled.println();

}


//-----------printTimeSerial------------------------
void printTimeSerial(DateTime now){
//------------------------------------------------
// printTimeSerial function takes a DateTime object from
// the real time clock and prints the date and time 
// to the serial monitor. 
  Serial.print(now.year(), DEC);
    Serial.print('-');
  if (now.month() < 10) {
    Serial.print(F("0"));
  }
    Serial.print(now.month(), DEC);
    Serial.print('-');
    if (now.day() < 10) {
    Serial.print(F("0"));
  }
  Serial.print(now.day(), DEC);
    Serial.print(' ');
  if (now.hour() < 10){
    Serial.print(F("0"));
  }
    Serial.print(now.hour(), DEC);
    Serial.print(':');
  if (now.minute() < 10) {
    Serial.print("0");
  }
    Serial.print(now.minute(), DEC);
    Serial.print(':');
  if (now.second() < 10) {
    Serial.print(F("0"));
  }
    Serial.print(now.second(), DEC);
  // You may want to print a newline character
  // after calling this function i.e. Serial.println();

}


//--------------updateSunriseSunset---------------------------------
/*! updateSunriseSunset function
 *  @param DateTime newtime - date/time object from real time clock
 *  @param byte oldday - numeric value of previous day
 * 
 */
void updateSunriseSunset(DateTime newtime, byte oldday){
  if (oldday != newtime.day()){
        // If we've turned over a new day, check to see if it's a new 
        // month, and if so we can adjust the sunrise/sunset hours
        oldday = newtime.day(); // update oldday
        // Set sunriseHour and sunsetHour to roughly encompass the
        // Sitka Alaska day lengths, all in UTC-9 time zone
        switch(newtime.month()){
          case 1:
            // January
            sunriseHour = 9;
            sunsetHour = 15;
          break;
          case 2:
            // February            
            sunriseHour = 8;
            sunsetHour = 16;
          break;
          case 3:
            // March
            sunriseHour = 8;
            sunsetHour = 17;
          break;
          case 4:
            // April
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 5:
            // May
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 6:
            // June
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 7:
            // July
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 8:
            // August
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 9:
            // September
            sunriseHour = 7;
            sunsetHour = 20;
          break;
          case 10:
            // October
            sunriseHour = 7;
            sunsetHour = 17;
          break;
          case 11:
            // November
            sunriseHour = 8;
            sunsetHour = 16;
          break;
          case 12:
            // December
            sunriseHour = 9;
            sunsetHour = 15;
          break;
          
        } // end of switch(newtime.month())
      }

}
