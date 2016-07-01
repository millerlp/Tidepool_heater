/* temp_sense_test2.ino
	Luke Miller 2016-07

	Use a DS18B20 temperature sensor probe to track water 
	temperature as a heater heats it.
  Record data to an SD card to allow standalone operation.
  Potentially show data on OLED display screen
	
	The hardware includes two OneWire DS18B20 temperature
	sensor, one for the warmed water and one for the 
	ambient water. The individual sensor addresses are 
	hard coded in the preamble below and would need to be
	updated to use other sensors. 
	
	The heating is accomplished by a 12VDC silicone heating 
	pad connected to a MOSFET attached to pin 8 of the 
	arduino.
	
*/

// Include the libraries we need
//#include <Wire.h>
#include <SPI.h>
#include <OneWire.h>  // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
//#include <SdFat.h>  // https://github.com/greiman/SdFat
#include "SSD1306Ascii.h" // https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiAvrI2c.h" // https://github.com/greiman/SSD1306Ascii

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
unsigned long maxHeatTime = 400; // time heater is allowed to run (seconds)
unsigned long updateTime = 2; // time to update Serial monitor (seconds)
unsigned long lastTime = 0; // variable to keep previous time value

// Temperature variables
float maxTempC = 29.0; // maximum temperature (C) allowed before shutting off heater
float warmWaterTempC = 0; // current heated water temperature
float ambientWaterTempC = 0; // ambient water temperature

//*************************
#define MOSFET 8  // Arduino digital pin used to turn on MOSFET

// Define the OLED display object
SSD1306AsciiAvrI2c oled;
#define I2C_ADDRESS 0x3C
#define OLED_RESET 4 // Digital pin hooked to OLED reset line

//**********************
#define BUTTON1 2 // Digital pin hooked to button

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

  // Initialize the OLED display
  oled.reset(OLED_RESET);
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  oled.setFont(Adafruit5x7);
  oled.clear();  
  
	myMillis = millis();
	// Poll the temperature sensors
	sensors.requestTemperatures();
	warmWaterTempC = sensors.getTempC(warmedThermometer);
  ambientWaterTempC = sensors.getTempC(ambientThermometer);
  
	Serial.println(warmWaterTempC);
  // Use function to print temperatures to OLED display
  PrintoledTemps();

  Serial.println(F("Hit start button"));
  oled.println();
  oled.println (F("Hit start button"));     // signal initalization done

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
    } 
  }
	// Turn on heater
	digitalWrite(MOSFET, HIGH); 

}


void loop (void)
{

	// In the main loop, run the heater until the elapsed
	// time exceeds maxHeatTime. After that just kill the heater
	while ( (millis() - myMillis < maxHeatTime) & (warmWaterTempC < maxTempC)) {
		if (millis() - lastTime > updateTime) {
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
		}
	}
	// If the while loop above quits for any reason, kill the heater
	digitalWrite(MOSFET, LOW); // turn off heater
	Serial.println(F("Shutting off heat"));

	// Go into infinite loop, only to be quit via hardware reset
	while(1){
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


