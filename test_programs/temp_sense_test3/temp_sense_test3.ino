/* temp_sense_test3.ino
	Luke Miller 2016-07

	Use a DS18B20 temperature sensor probe to track water 
	temperature as a heater heats it.
  
  Show data on OLED display screen.

  Save temperature data to a SD card. 
	
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

  The Reset pin for the OLED I2C display is attached to 
  Arduino pin 4

  
	
*/

// Include the libraries we need
#include <Wire.h>
#include <SPI.h>
#include <OneWire.h>  // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include <SdFat.h>  // https://github.com/greiman/SdFat
#include "SSD1306Ascii.h" // https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiWire.h" // https://github.com/greiman/SSD1306Ascii


// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 3
// Define the bits of temperature precision for DS18B20
#define TEMPERATURE_PRECISION 12
// Setup a oneWire instance to communicate with any 
// OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);
// Arrays to hold OneWire device addresses
  // Assign address manually. The addresses below will need to be changed
  // to valid device addresses on your bus. Device address can be retrieved
  // by using either oneWire.search(deviceAddress) or individually via
  // sensors.getAddress(deviceAddress, index)
DeviceAddress warmedThermometer = { 0x28, 0x00, 0x39, 0xA3, 0x06, 0x0, 0x0, 0x99 };
DeviceAddress ambientThermometer = { 0x28, 0x82, 0x2C, 0xA3, 0x06, 0x0, 0x0, 0xE3 };

// Timekeeping
unsigned long myMillis = 0;
unsigned long maxHeatTime = 14400; // time heater is allowed to run (seconds)
unsigned long updateTime = 2; // time to update Serial monitor (seconds)
unsigned long SDupdateTime = 10; // time to write data to SD card (seconds)
unsigned long lastTime = 0; // variable to keep previous time value
unsigned long lastSDTime = 0; // variable to keep previous SD write time value

// Temperature variables
float maxTempC = 29.0; // maximum temperature (C) allowed before shutting off heater
float warmWaterTempC = 0; // current heated water temperature
float ambientWaterTempC = 0; // ambient water temperature

//*************************
#define MOSFET 8  // Arduino digital pin used to turn on MOSFET

//*********************************************
// Define the OLED display object
SSD1306AsciiWire oled;  // When using Wire library
#define I2C_ADDRESS 0x3C
#define OLED_RESET 4 // Digital pin hooked to OLED reset line

//**********************
#define BUTTON1 2 // Digital pin hooked to button

//********************************
// SD chip select pin.
const uint8_t SD_CHIP_SELECT = 10;
//*************
// Create sd card objects
SdFat sd;
SdFile logfile;  // for sd card, this is the file object to be written to
// Declare initial name for output files written to SD card
char filename[] = "Temps_00.csv";
bool sdErrorFlag = false; // false = no error, true = error

//**********************************************************
//**********************************************************
void setup(void)
{
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);
  pinMode(BUTTON1, INPUT_PULLUP);

  // start serial port
  Serial.begin(57600);
  Serial.println(F("Hello"));

  // Start up the OneWire library
  sensors.begin();

  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  
  // Search for devices on the bus and assign based on an index. Ideally,
  // you would do this to initially discover addresses on the bus and then 
  // use those addresses and manually assign them (see above) once you know 
  // the devices on your bus (and assuming they don't change).
  // 
  // method 1: by index
  if (!sensors.getAddress(warmedThermometer, 0)) Serial.println("Unable to find address for Device 0"); 
  if (!sensors.getAddress(ambientThermometer, 1)) Serial.println("Unable to find address for Device 1"); 

	// If you initially don't know the addresses of your OneWire units,
	// Uncomment the code below and run it. The addresses (8 hex bytes) 
	// of attached OneWire units will be saved into warmedThermometer and
	// ambientThermometer in the order they're found on the bus. Try searching
	// with only one unit plugged in and record the address that is output. 
	// Then plug in the other unit, restart, and record the new address that
	// shows up.
	// method 2: manual search, assign found units to the inside/out
  // oneWire.reset_search();
  // if (!oneWire.search(warmedThermometer)) Serial.println("Unable to find address for insideThermometer");
  // if (!oneWire.search(ambientThermometer)) Serial.println("Unable to find address for outsideThermometer");
    
	// show the addresses we found on the bus
	Serial.print("Device 0 Address: ");
	printAddress(warmedThermometer);
	Serial.println();

	Serial.print("Device 1 Address: ");
	printAddress(ambientThermometer);
	Serial.println();
	
	// Set the temperature resolution
	sensors.setResolution(warmedThermometer, TEMPERATURE_PRECISION);
	sensors.setResolution(ambientThermometer, TEMPERATURE_PRECISION);
  
  // Convert maxHeatTime and updateTime to milliseconds
	maxHeatTime = maxHeatTime * 1000;
	updateTime = updateTime * 1000;
  SDupdateTime = SDupdateTime * 1000;

  // Initialize the OLED display
  oled.reset(OLED_RESET);
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  oled.setFont(Adafruit5x7);
  oled.clear();  
  
	
	// Poll the temperature sensors
	sensors.requestTemperatures();
	warmWaterTempC = sensors.getTempC(warmedThermometer);
  ambientWaterTempC = sensors.getTempC(ambientThermometer);
  
	Serial.println(warmWaterTempC);
  // Use function to print temperatures to OLED display
  PrintoledTemps();

  //**********************
  // Set up SD card
  pinMode(SD_CHIP_SELECT, OUTPUT);  // set chip select pin for SD card to output
  if (!sd.begin(SD_CHIP_SELECT, SPI_FULL_SPEED)) {
    sdErrorFlag = true; 
  } else {
    sdErrorFlag = false;
    initFileName();
  }
  

  Serial.println(F("Hit start button"));


  // Wait here for button press
  while(digitalRead(BUTTON1) != LOW) {
      if (millis() - lastTime > updateTime){
      // Update lastTime
      lastTime = millis();
      // Send the command to get all available temperatures
      sensors.requestTemperatures(); 
      // currWaterTempC = sensors.getTempCByIndex(0);
      warmWaterTempC = sensors.getTempC(warmedThermometer);
      ambientWaterTempC = sensors.getTempC(ambientThermometer);
      // Use function to print temperatures to OLED display
      PrintoledTemps();
      oled.println();
      oled.println(F("Hit start button"));
      if (sdErrorFlag){
        oled.print(F("SD not found"));
      }
    } 
  }
	// Turn on heater
	digitalWrite(MOSFET, HIGH); 
  // Set myMillis to denote start time
  myMillis = millis();

}


void loop (void)
{

	// In the main loop, run the heater until the elapsed
	// time exceeds maxHeatTime. After that just kill the heater
	while ( (millis() - myMillis < maxHeatTime) & (warmWaterTempC < maxTempC)) {
		if ( (millis() - lastTime) > updateTime) {
			// Update lastTime
			lastTime = millis();	
      
			// Send the command to get all available temperatures
			sensors.requestTemperatures(); 
			// currWaterTempC = sensors.getTempCByIndex(0);
			warmWaterTempC = sensors.getTempC(warmedThermometer);
			ambientWaterTempC = sensors.getTempC(ambientThermometer);
			// Output a new temperature to Serial
			Serial.print(F("Heated: "));
			Serial.print(warmWaterTempC);
			Serial.print(F("C \t ambient: "));
			Serial.print(ambientWaterTempC);
			Serial.println(F("C"));
      // Use function to print temperatures to OLED display
      PrintoledTemps();
      oled.println();
      oled.println(F("Heating"));
      // Include the elapsed heating time
      oled.print(F("Elapsed sec: "));
      oled.println( (millis() - myMillis)/1000);
      // Check if it has been long enough to write another
      // sample to the SD card
      if ( (millis() - SDupdateTime) > lastSDTime) {
        lastSDTime = millis();
        writeToSD();
      }
		} // end of if (millis() - lastTime > updateTime)
	} // end of heating while loop
 
	// If the while loop above quits for any reason, kill the heater
	digitalWrite(MOSFET, LOW); // turn off heater
	Serial.println(F("Shutting off heat"));

	// Go into infinite loop, only to be quit via hardware reset
	while(1){
	  if ( (millis() - lastTime) > updateTime){
      // Update lastTime
      lastTime = millis();
      // Send the command to get all available temperatures
      sensors.requestTemperatures(); 
      // currWaterTempC = sensors.getTempCByIndex(0);
      warmWaterTempC = sensors.getTempC(warmedThermometer);
      ambientWaterTempC = sensors.getTempC(ambientThermometer);
	    // Use function to print temperatures to OLED display
      PrintoledTemps();
      oled.println();
      oled.println(F("Finished"));
	  }
	}

}


//---------------printAddress-------------------------------------------
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
 *  temperatures to the OLED display
 */
void PrintoledTemps(void)
{
  oled.clear();
  oled.print(F("Warmed: "));
  oled.print(warmWaterTempC);
  oled.println(F("C"));
  oled.print(F("Ambient: "));
  oled.print(ambientWaterTempC);
  oled.print(F("C"));
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

  logfile.println(F("Seconds,WarmedTempC,AmbientTempC"));
  logfile.close(); // force the data to be written to the file by closing it
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
  logfile.print(ambientWaterTempC);
  logfile.println();
  
  logfile.close(); // force the buffer to empty
}
