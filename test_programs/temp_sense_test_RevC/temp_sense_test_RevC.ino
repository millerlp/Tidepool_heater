/* temp_sense_test_RevA.ino
	Luke Miller 2018-12

 For use with Tidepool_heater_RevA hardware board

	Use a DS18B20 temperature sensor probe to track water 
	temperature as a heater heats it.
  
  Show data on OLED display screen.

  Save temperature data to a SD card. 

  Use INA219 chip to sense battery voltage and current
	
	The hardware includes two OneWire DS18B20 temperature
	sensors, one for the warmed water and one for the 
	ambient water. The individual sensor addresses are 
	hard coded in the preamble below and would need to be
	updated to use other sensors. The data line for the 
  OneWire devices is Arduino pin 3
	
	The heating is accomplished by a 12VDC silicone heating 
	pad connected to a MOSFET attached to pin 8 of the 
	Arduino.

  A tactile switch button is attached to Arduino pin 2 and
  ground. 

*/

// Include the libraries we need
#include <Wire.h>
#include <SPI.h>
#include <OneWire.h>  // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include <SdFat.h>  // https://github.com/greiman/SdFat
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
//*******************************
// Define the bits of temperature precision for DS18B20
#define TEMPERATURE_PRECISION 12
// Setup a oneWire instance to communicate with any 
// OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);
// Arrays to hold OneWire device addresses
// Device address can be retrieved
// by using either oneWire.search(deviceAddress) or individually via
// sensors.getAddress(deviceAddress, index)
DeviceAddress warmedThermometer;

// Timekeeping
unsigned long myMillis = 0;
unsigned long maxHeatTime = 5 ; // time heater is allowed to run (HOURS)
unsigned long updateTime = 2; // time to update Serial monitor (seconds)
unsigned long SDupdateTime = 10; // time to write data to SD card (seconds)
unsigned long lastTime = 0; // variable to keep previous time value
unsigned long lastSDTime = 0; // variable to keep previous SD write time value
bool quitFlag = false; // Flag to quit the heating loop
// Temperature variables
float maxTempC = 37.0; // maximum temperature (C) allowed before shutting off heater
float warmWaterTempC = 0; // current heated water temperature
float ambientWaterTempC = 0; // ambient water temperature

//*********************************************
// Define the OLED display object
SSD1306AsciiWire oled;  // When using Wire library
#define I2C_ADDRESS 0x3C

//**********************
#define COMMON_ANODE 
#define BUTTON1 2 // Digital pin D2 (PD2) hooked to button 1
#define REDLED 9 // Red LED 
#define GRNLED 5 // Green LED
#define BLULED 6 // Blue LED

//********************************
// SD chip select pin.
const uint8_t SD_CHIP_SELECT = 10;
//*************
// Create sd card objects
SdFat sd;
SdFile logfile;  // for sd card, this is the file object to be written to
// Declare initial name for output files written to SD card
char filename[] = "Temps_00.csv";
volatile bool sdErrorFlag = false; // false = no error, true = error

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

//**********************************************************
//**********************************************************
void setup(void)
{
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(REDLED, OUTPUT);
  pinMode(GRNLED, OUTPUT);
  pinMode(BLULED, OUTPUT);
  digitalWrite(REDLED, HIGH); // for common anode LED, set high to shut off
  digitalWrite(GRNLED, HIGH);
  digitalWrite(BLULED, HIGH);
  // start serial port
  Serial.begin(57600);
  Serial.println(F("Hello"));

  // Start up the OneWire library
  sensors.begin();

  Serial.print(F("Found "));
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(F(" OneWire devices."));
  
  // Search for OneWire devices on the bus and assign based on an index. 
	// The address (8 hex bytes) of attached OneWire unit will be saved 
	// into warmedThermometer. 
//   oneWire.reset_search();
   if (!oneWire.search(warmedThermometer)) Serial.println("Unable to find address for DS18B20 Thermometer");

    
	// show the addresses we found on the bus
	Serial.print("Device 0 Address: ");
	printAddress(warmedThermometer);
	Serial.println();

	
	// Set the temperature resolution
	sensors.setResolution(warmedThermometer, TEMPERATURE_PRECISION);
  
  // Convert maxHeatTime and updateTime to milliseconds
	maxHeatTime = maxHeatTime * 60 * 60 * 1000;
	updateTime = updateTime * 1000;
  SDupdateTime = SDupdateTime * 1000;
  //*********************************
  // Initialize the OLED display
  oled.begin(&Adafruit128x64, I2C_ADDRESS);  // For 128x64 OLED screen
  oled.setFont(Adafruit5x7);
  oled.clear();  
  //****************************
	// Poll the temperature sensors
	sensors.requestTemperatures();
	warmWaterTempC = sensors.getTempC(warmedThermometer);
  
  //**********************
  // Set up SD card
  pinMode(SD_CHIP_SELECT, OUTPUT);  // set chip select pin for SD card to output
  if (!sd.begin(SD_CHIP_SELECT, SPI_HALF_SPEED)) {
    sdErrorFlag = true; 
  } else {
    sdErrorFlag = false;
//    initFileName();
  }
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
  
  Serial.println(warmWaterTempC);

  Serial.println(F("Hit start button"));

  //***********************************************************
  // Wait here for button press. Heating will not start
  // until the user chooses to start. Meanwhile, output
  // current temperatures, battery voltage
  while(digitalRead(BUTTON1) != LOW) {
      if (millis() - lastTime > updateTime){
      // Update lastTime
      lastTime = millis();
      // Send the command to get all available temperatures
      sensors.requestTemperatures(); 
      warmWaterTempC = sensors.getTempC(warmedThermometer);
      // Update voltage and current data
      busvoltage = ina219.getBusVoltage_V();
      current_mA = ina219.getCurrent_mA();
      shuntvoltage = ina219.getShuntVoltage_mV();
      loadvoltage = busvoltage + (shuntvoltage / 1000);
      // Update serial monitor
      printSerial();
      // Use function to print temperatures+voltage to OLED display
      PrintoledTemps();
      oled.println();
      // 4th line, show info
      oled.print(F("Hit start button "));
      oled.println();
      if (sdErrorFlag){
        oled.print(F("No SD"));
      }
      // Pulse LED
      digitalWrite(BLULED, !digitalRead(BLULED));
    } 
  }
  //***************************************************
	// Turn on heater
	digitalWrite(MOSFET, HIGH); 
  digitalWrite(BLULED, HIGH); // Turn off blue LED if on
  oled.clear();
  oled.print(F("Starting..."));
  Serial.println(F("Starting heater"));
  delay(200);
//  digitalWrite(REDLED, LOW); // Turn on red led to show heater power is on
  setColor(60,0,0); // red color
  wdt_enable(WDTO_4S); // Enable 4 second watchdog timer timeout
  // Set myMillis to denote start time
  myMillis = millis();
}  // end of setup loop

/////////////////////////////////////////////////////////// Main loop
void loop (void)
{
  // Start by initializing the output file 
  if (!sdErrorFlag){
    initFileName();
  }
	// In the main loop, run the heater until the elapsed
	// time exceeds maxHeatTime or the battery supply voltage 
	// drops below the voltageMin. After that just kill the heater.
  while ( (millis() - myMillis < maxHeatTime) & (warmWaterTempC < maxTempC) & 
	(loadvoltage > voltageMin) )
  {
    // Reset the watchdog timer every time the while loop loops
    wdt_reset(); 
    // If a few seconds have elapsed (set by updateTime), take 
    // new temperatures and update display
  		if ( (millis() - lastTime) > updateTime) {
  			// Update lastTime
  			lastTime = millis();	
      
  			// Send the command to get all available temperatures
  			sensors.requestTemperatures(); 
  			// Write temperatures to the separate variables
  			warmWaterTempC = sensors.getTempC(warmedThermometer);
        // Update busvoltage
        busvoltage = ina219.getBusVoltage_V();
        current_mA = ina219.getCurrent_mA();
        shuntvoltage = ina219.getShuntVoltage_mV();
        loadvoltage = busvoltage + (shuntvoltage / 1000);
      
	    // Output new info to Serial monitor
        printSerial();
      // Use function to print temperatures to OLED display
        PrintoledTemps();
        oled.println();
      // Include the elapsed heating time
        oled.print(F("Time: "));
        oled.print( (millis() - myMillis)/1000);
        oled.print(F(" Heating"));
        oled.println();
        if (sdErrorFlag){
          oled.print(F("No SD"));
        } else {
          oled.print(F("     "));
        }
      // Check if it has been long enough to write another
      // sample to the SD card (set by lastSDTime)
        if ( (millis() - SDupdateTime) > lastSDTime) {
          lastSDTime = millis();
          if (!sdErrorFlag){
            writeToSD();
          }
        }
		} // end of if (millis() - lastTime > updateTime)
   if (quitFlag){ // If someone hits BUTTON1 interrupt again to quit
    break; // Leave the heating loop, proceed to the infinite loop below
   }
   // Enable button1 interrupt to quit this loop when user presses button1
   attachInterrupt(0, buttonFunc, LOW); 
  } // end of heating while loop
 
 // If the while loop above quits for any reason, kill the heater
  digitalWrite(MOSFET, LOW); // turn off heater
  setColor(0,0,0); // turn off notification LED

  // Go into infinite loop, only to be quit via hardware reset
  while(1) {
    if ( (millis() - lastTime) > updateTime)
    {
      // Update lastTime
      lastTime = millis();
      // Send the command to get all available temperatures
      sensors.requestTemperatures(); 
      warmWaterTempC = sensors.getTempC(warmedThermometer);
      // Update busvoltage
      busvoltage = ina219.getBusVoltage_V();
      current_mA = ina219.getCurrent_mA();
      shuntvoltage = ina219.getShuntVoltage_mV();
      loadvoltage = busvoltage + (shuntvoltage / 1000);
    // Use function to print temperatures+voltage to OLED display
      PrintoledTemps();
      oled.println();
      oled.print(F("Finished"));
      oled.println();
      if (sdErrorFlag){
          oled.print(F("No SD"));
      } else {
         oled.print(F("     ")); 
      }
      oled.println();
      if (lowVoltageFlag){
        Serial.println(F("Hit voltage min."));
        oled.print(F("Hit voltage min."));
      }
      // Check if it has been long enough to write another
      // sample to the SD card (set by lastSDTime)
      if ( (millis() - SDupdateTime) > lastSDTime) {
        lastSDTime = millis();
        writeToSD();

      }
      wdt_reset();
//      digitalWrite(GRNLED, LOW); // flash on
      setColor(0,60,0);
      delay(200);
      setColor(0,0,0);
//      digitalWrite(GRNLED, HIGH); // shut off again
    } // End of if statement
  } // End of while loop

}


//---------------printAddress--------------------------------------
// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

//----------PrintoledTemps------------------
/* Function to print warm and ambient water
 *  temperatures, and battery voltage 
 *  to the OLED display.
 *  After calling this function send another
 *  oled.println() call if you need to write
 *  a 4th line of info
 */
void PrintoledTemps(void)
{
  oled.clear();
  oled.print(F("Warmed: "));
  oled.print(warmWaterTempC);
  oled.println(F(" C"));
  // 2nd line, show battery voltage
  oled.print(busvoltage);
  oled.println(F(" V bus"));
  oled.print(current_mA);
  oled.println(F("mA"));
  oled.print(loadvoltage);
  oled.println(F(" V battery"));
}

//-------------- initFileName --------------------------------------------------
// initFileName - a function to create a filename for the SD card 
// with a 2-digit counter in the name. 
// The character array 'filename' was defined as a global array 
// at the top of the sketch in the form "Temps_00.csv"
void initFileName(void) {
  
  // Change the counter on the end of the filename
  // (digits 6+7) to increment count for files generated on
  // the same day. 
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i / 10 + '0';
    filename[7] = i % 10 + '0';
    
    if (!sd.exists(filename)) {
      // when sd.exists() returns false, this block
      // of code will be executed to open the file
      if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
        // If there is an error opening the file, notify the
        // user. Otherwise, the file is open and ready for writing
        sdErrorFlag = true;
      }
      break; // Break out of the for loop when the
      // statement if(!logfile.exists())
      // is finally false (i.e. you found a new file name to use).
    } // end of if(!sd.exists())
  } // end of file-naming for loop
  //------------------------------------------------------------
  // Write 1st header line to SD file based on mission info
  logfile.println(F("Seconds,WarmedTempC,BatteryV,Current.mA"));
  
  logfile.close(); // Force the data to be written to the file by closing it
} // end of initFileName function


//------------- writeToSD -----------------------------------------------
// writeToSD function. This formats the available data in the
// data arrays and writes them to the SD card file in a
// comma-separated value format.
void writeToSD (void) {

  // Reopen logfile. If opening fails, notify the user
  if (!logfile.isOpen()) {
    if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
      sdErrorFlag = true; // If there is an error
    } else {
      sdErrorFlag = false; // File opened successfully
    }
  }

  // Write elapsed seconds to first column
  logfile.print( (millis() - myMillis)/1000);
  logfile.print(F(","));
  logfile.print(warmWaterTempC);
  logfile.print(F(","));
  logfile.print(loadvoltage);
  logfile.print(F(","));
  logfile.print(current_mA);
  logfile.println();
  
  logfile.close(); // force the buffer to empty
}

//-----------printSerial--------------------
void printSerial (void)
{
      Serial.print(F("Heated: "));
      Serial.print(warmWaterTempC);
      Serial.print(F("C \t"));
      Serial.print(F("Bus Voltage: "));
      Serial.print(busvoltage);
      Serial.print(F(" V\t Load Voltage: "));
      Serial.print(loadvoltage);
      Serial.print(F(" V\t current: "));
      Serial.print(current_mA);
      Serial.println(F("mA"));
}

//--------------- buttonFunc --------------------------------------------------
// buttonFunc
void buttonFunc(void){
  detachInterrupt(0); // Turn off the interrupt
  delay(20);
  if (digitalRead(BUTTON1) == LOW){
    quitFlag = true; // Button has been pressed for at least 20ms, quit heating loop
  }
  // Execution will now return to wherever it was interrupted, and this
  // interrupt will still be disabled. 
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
  analogWrite(BLULED, blue);  
}
