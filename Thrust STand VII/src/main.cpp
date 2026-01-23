#include <Arduino.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HX711.h>
#include <Servo.h> //
#include <avr/wdt.h> //watchdog for resetting the test if there's a hardware failure
#include <EEPROM.h> //eeprom stores the thrust and torque calibrations
#include <SPI.h> //used for the Spi needed for the SD card
#include <SD.h> //used for the SD card

/*TODO: 
Thrust Profiles
STD Dev of Sensors Calculations
Airspeed
Airspeed overwrite
SD Card Reading
EEPROM Calibration Saving
RPM Verification
*/

//////////////////////////////////////////////////////////////////////////////////////////////////
//FUNCTION EXTERNALS
extern void debugMenu();
extern void runTest();
extern void zeroAnalog();

//////////////////////////////////////////////////////////////////////////////////////////////////
//EEPROM Variables

#define THST_CAL_ADDRESS 0
#define TRQ_CAL_ADDRESS 100 //make sure this is sufficiently spaced from thst cal to avoid overwriting

//////////////////////////////////////////////////////////////////////////////////////////////////
//Test Variables;
const int testDataInterval = 200; //in milliseconds, the amount of time between sensor reading and data writing cycles

float testStartTime = 0;
float testTime = 0;
float thrust = 0; //mN
float torque = 0; //N.mm
float airspeed = 0; //m/s
float current = 0; //amps
float voltage = 0; //volts
int RPM = 0;
int throttle = 0;

//Calculated Variables
float electricPower = 0; //watts
float mechanicalPower = 0; //watts
float propellerPower = 0; //watts
float motorEfficiency = 0; //0-100%
float propellerEfficiency = 0; //0-100%
float systemEfficiency = 0; //0-100%

//////////////////////////////////////////////////////////////////////////////////////////////////
//SD CARD

const int SD_CS_PIN = 53;     // Change if your module uses a different CS
File dataFile; //used for the arduino to write to
const int flushPeriodMillis = 5000; //this is how often the arduino will flush (save to the SD card) while doing a test
int lastFlush = 0; 

//////////////////////////////////////////////////////////////////////////////////////////////////
//LOAD CELLS

HX711 thrustSensor;
HX711 torqueSensor;

#define TRQ_DOUT 48
#define TRQ_CLK 49
#define TRQ_UNITS "(N.mm)"

#define THST_DOUT 46
#define THST_CLK 47
#define THST_UNITS "(mN)"

extern void tareTorque(); //these need to be here so the menu structure knows these exist before they're declared in the file
extern void calibrateTorque();

extern void tareThrust();
extern void calibrateThrust();

///////////////////////////////////////////////////////////////////////////////////////
// CURRENT AND VOLTAGE SENSOR DEFINITIONS

#define CURRENT_PIN A2
#define VOLTAGE_PIN A3

const float Vcc = 5.0; //change to match VCC logic voltage of board
const float CURRENT_SENSITIVITY = 0.020;
float CURRENT_OFFSET;
float VOLTAGE_OFFSET;
const float VOLTAGE_CALIBRATION = 21;

long averageGain = 25; //how strong the moving average is for moving average sensors 
const float averageCount = 40; //this controls how many averages the reading will take

////////////////////////////////////////////////////////////////////////////////////////
//RPM Config

const byte rpmPin = 2;                  // Interrupt pin
const unsigned long window_ms = 250;    // RPM update window
long pulsesPerRev = 1; //number of markers

volatile unsigned long lastPulseMicros = 0;
volatile unsigned long currentPulseMicros = 0;

//interrupt service routine
void rpmISR() {
    lastPulseMicros = currentPulseMicros;
    currentPulseMicros = micros();
}

///////////////////////////////////////////////////////////////////////////////////////
//AIRSPEED SENSOR

long airspeedOverride = 0;
#define AIRSPEED_PIN A7
float zeroVoltage = 2.7;    // MODIFY THIS VALUE TO CORRESPOND TO VOLTAGE WITHOUT ANY AIRFLOW
#define sensitivity 1   // Sensor sensitivity in V/kPa
#define airDensity 1.2    // Air density at sea level in kg/m^3

////////////////////////////////////////////////////////////////////////////////////////
//THROTTLE LOGIC DEFINITIONS
//(For a HARGRAVE MICRODRIVE ESC, accepted PWM frequencies range from 50Hz to 499 Hz

Servo esc; 
const int MIN_THROTTLE = 950;
const int MAX_THROTTLE = 2000;
const int ESC_PIN = 3;

//-----------------------------------------GLOBAL VARIABLES-----------------------------------

//UI
#define USER_NOTIF_DELAY 1800

long testNumber = 1;

//smooth ramp
long rampTime = 15; //in seconds
long topTime = 4; //in seconds
long smoothThrottleMax = 100; //as a percent
bool upDown = true; //if true, go up and then back down

//////////////////////////////////////////////////////////////////////////////////////////////////
//KEYBOARD SETUP
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

// 4x4 Matrix Keyboard
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Connect the row pins to the Arduino digital pins
byte rowPins[ROWS] = {12, 11, 10, 9}; // Adjust these if your wiring is different

// Connect the column pins to the Arduino digital pins
byte colPins[COLS] = {8, 7, 6, 5}; // Adjust these if your wiring is different

// Create the Keypad object
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

//////////////////////////////////////////////////////////////////////////////////////////////////
//SCREEN INITIALIZATION

//this is the magic that makes the screen work. Screen needs to be hooked up to SDA and SCL
// SSD1309, 128x64, I2C
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);


//////////////////////////////////////////////////////////////////////////////////////////////////
//MENU SETUP
/*
MENU FLOW CHART:
0 MAIN MENU
    1 Run Test
        Test Confirmation Info Screen
        OK Button
        Test Data Readout (of all sensors)

    2 Configure Test
        21 Test Profile Selection - Action
        22 Test Profile Configuration
            221 Smooth Up/Down
                2211 Ramp Time
                2212 Time at Top
                2213 Max Throttle
                2214 Up/Down Mode (Just up or up and down)
            222 Discreet Intervals
                2221 Interval Count
                2222 Interval Time
                2223 Max Throttle
                2224 Up/Down Mode (Just up or up and down)
        23 Test Setup Selection
            231 RPM Marker Count
            232 Test File Name

    3 Tare Sensors
        // 31 Zero All
        // 32 Tare Torque
        // 33 Tare Force
        // 34 Zero Force
        // 35 Zero Torque
        // 36 Zero Analog

    4 Debug Menu
        Display read values for all sensors,
*/
enum ItemType {TYPE_SUBMENU, TYPE_TOGGLE, TYPE_VALUE, TYPE_ACTION};

struct MenuItem {
    int itemId; //keep an eye on this, it could int overflow
    const char* label;
    ItemType type;
    int parentId;
    long* variable;
    void (*action)();
};

// Use the renamed enum TYPE_ACTION to avoid conflicts
MenuItem menus[] = {
    //MAIN MENU
    {0, "Main Menu", TYPE_SUBMENU, 1000, NULL, NULL},

    {1, "Run Test", TYPE_ACTION, 0, NULL, runTest},
    
    {2, "Configure Test", TYPE_SUBMENU, 0, NULL, NULL},
        {21, "Select Profile", TYPE_ACTION, 2, NULL, NULL},
        {22, "Configure Profiles", TYPE_SUBMENU, 2, NULL, NULL},
            {221, "Smooth", TYPE_SUBMENU, 22, NULL, NULL},
                {2211, "Ramp Up Time (s)", TYPE_VALUE, 221, &rampTime, NULL},
                {2212, "Top Hold Time (s)", TYPE_VALUE, 221, &topTime, NULL},
            {222, "Intervals", TYPE_SUBMENU, 22, NULL, NULL},
        {23, "Configure Hardware", TYPE_SUBMENU, 2, NULL, NULL},
            {231, "RPM Marker Count", TYPE_VALUE, 23, &pulsesPerRev, NULL},
            {232, "A-Spd Override (m/s)", TYPE_VALUE, 23, &airspeedOverride, NULL},
            {233, "Moving AVG Gain (0-100)", TYPE_VALUE, 23, &averageGain, NULL},

    {3, "Tare Sensors", TYPE_SUBMENU, 0, NULL, NULL},
        {32, "Zero Thrust", TYPE_ACTION, 3, NULL, tareThrust},
        {33, "Zero Torque", TYPE_ACTION, 3, NULL, tareTorque},
        {34, "Calibrate Thrust Sensor", TYPE_ACTION, 3, NULL, calibrateThrust},
        {35, "Calibrate Torque Sensor", TYPE_ACTION, 3, NULL, calibrateTorque},
        {36, "Zero Analog", TYPE_ACTION, 3, NULL, zeroAnalog},

    {4, "Debug", TYPE_ACTION, 0, NULL, debugMenu},
}; 

//this has to exist because calculating the number of items
//in a struct datatype is hard I guess
const int MENU_COUNT = sizeof(menus)/sizeof(menus[0]);
int currentMenuId = 0; //this keeps track of the current menu state


//////////////////////////////////////////////////////////////////////////////////////////////////
//UI FUNCTIONS

MenuItem* getMenu(int menuId) {//returns null if no Menu Item with that ID, otherwise returns a pointer to the item
    for (int i = 0; i < MENU_COUNT; i++) {
        if (menus[i].itemId == menuId) {
            return &menus[i]; //this is a pointer to the menu item.
        }
    }
    return nullptr; //returns null if there's no menu with that ID
}

void pressKeyToContinue(){
    //wait for user to acknowledge
    while (customKeypad.getKey() == NO_KEY){
        //do nothing and wait for them to press a key
    }
}

void drawMenu(int menuId) { //pass the ID of the parent menu. Will fetch all submenus and display them
    u8g2.clearBuffer(); //prepare the screen for writing
    u8g2.setFont(u8g2_font_6x12_tr); //set big font for parent menu

    MenuItem* parentMenu = getMenu(menuId); //grab a pointer to the top menu, if that exists.
    if (parentMenu) {
        u8g2.drawStr(2, 9, parentMenu->label); //access the label of the menu (that's the ->), then print it
    }

    u8g2.drawLine(0, 10, 128, 10); //draw line across bottom

    int menusDrawn = 1; //keep track of how many sub menus we draw so that we can keep moving down
    int menuOffset = 7; //pixel height of one menu item
    u8g2.setFont(u8g2_font_squeezed_r6_tr); //set small font for submenus
    for (int i = 0; i < MENU_COUNT; i++) {
        if (menus[i].parentId == menuId) {
            MenuItem* subMenu = getMenu(menus[i].itemId); //grab the menu item pointer (this is like super bad and slow but it works!)

            u8g2.setCursor(4, 12 + menusDrawn*menuOffset);
            u8g2.print(menusDrawn); //this prints the option number
            u8g2.drawStr(12, 12 + menusDrawn*menuOffset, subMenu->label); //this prints the name of the menu

            if (subMenu->type == TYPE_VALUE) { //if the value exists and isn't a null pointer, print it at the end
                Serial.print("Menu item is a value");
                if (subMenu->variable != nullptr){
                    u8g2.setCursor(95, 12 + menusDrawn*menuOffset);
                    u8g2.print("= ");
                    u8g2.print(*(subMenu->variable));
                }
            }


            menusDrawn++; //increment the counter so we draw the next one lower
        }
    }

    u8g2.drawStr(4, 63, "Back: *");
    u8g2.sendBuffer();
}

int getChosenMenuId(int choice) { //given an the int of the choice (1 index), then it will find the chosen menu Id. If the choice is invalid, then returns -1

    //fetch that menus pointer by going through the all of the options and counting down by the chosen number.
    for (int i = 0; i < MENU_COUNT; i++) {
        if (menus[i].parentId == currentMenuId) { 
            if (choice == 1) {
                return menus[i].itemId;
            } else if (choice < 1){
                return -1;
            } else {
                choice = choice - 1;
            }
        }
    }
    return -1;
}

void valueEditMenu(long* value, const char* label){ //pass this method a pointer to an int and a label to show for the int. It will give the user the UI to type in any positive integer of 8 digits or less.
    Serial.println("Inside value edit menu!");
    if (!value){
        Serial.println("value doesn't exist, returning");
        return;
    }

    int startTime = millis(); //we track how long since it started so that we can time out
    Serial.print("Value is: "); Serial.println(*value);
    Serial.print("Label is: "); Serial.println(label);
    //set up the input string
    String input = String(*value);

    while(1){
        //set up for entry
        u8g2.clearBuffer();
        u8g2.setFontMode(1);
        u8g2.setBitmapMode(1);
        u8g2.setFont(u8g2_font_t0_12b_tr);
        u8g2.drawStr(2, 11, label);
        u8g2.drawLine(0, 13, 127, 13);
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(89, 51, "Accept: # ");
        u8g2.drawStr(89, 57, "Delete: D");
        u8g2.drawStr(89, 63, "Cancel: *");
        u8g2.setFont(u8g2_font_t0_22b_tr);
        
        //print the input string
        u8g2.setCursor(3, 40);
        u8g2.print(input);

        if ((millis()-startTime)/300 % 2 == 1){ //check time to do a cursor blink. Uses modulo to decide whethere it's an "even" or "odd" time
            u8g2.print("|");
        }
        
         //wait for the user to press a key
        char userInput = customKeypad.getKey();
    
        // If a key is pressed, print it to the Serial Monitor
        if (userInput) {
            Serial.println(userInput);

            //check to see if the key is a number, if it is then we should put the number into the string
            if (userInput >= '0' && userInput <= '9') {
                if (input.length() < 8){ //make sure the number doesn't get too long for int overflow!
                    input += userInput;
                }

            //asterisk is the cancel button
            } else if (userInput == '*') {
                Serial.println("Cancel");
                return;
            
            //pound is the confirm button
            } else if (userInput == '#') {
                *value = input.toInt();
                return;

            //D is the delete button
            } else if (userInput == 'D') {
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                }
            }
        } 
        u8g2.sendBuffer();
    }
}

void executeMenu(int targetMenuId) {
    MenuItem targetMenu = *getMenu(targetMenuId);
    if (targetMenu.type == TYPE_SUBMENU) {
        Serial.println("Submenu Type!");
        currentMenuId = targetMenuId; //navigate the submenu if it's a submenu item
    } else if (targetMenu.type == TYPE_ACTION) {
        Serial.println("Action Type!"); 
        if (targetMenu.action){
            targetMenu.action(); //all the function if it's a function menu item
        }
    } else if (targetMenu.type == TYPE_VALUE) {
        Serial.println("Value Type!");
        valueEditMenu(targetMenu.variable, targetMenu.label);
    } else if (targetMenu.type == TYPE_TOGGLE) {
        Serial.println("Toggle Type!");
        //write bool change function here
    }
}

void drawLoadingScreen(int loadPercent, const char* message){//pass load percent as an int from 0-100

    Serial.println(message);

    u8g2.clearBuffer();

    //this is the bitmap for the uw logo
    static const unsigned char image_fzx3lqe_image_bits[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x02,0x00,0x00,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x10,0xf8,0x00,0x00,0x00,0x00,0xe0,0xe3,0x03,0x00,0x00,0x00,0x00,0x8f,0x0f,0x05,0x00,0x00,0x3c,0x7c,0xff,0x0f,0x00,0x00,0xf0,0xf1,0xff,0x07,0x00,0x00,0x80,0xe7,0xff,0x07,0x00,0x00,0x00,0xbe,0xff,0x09,0x00,0x00,0x00,0xfc,0xff,0x00,0x00,0x00,0x00,0xf8,0xbf,0x01,0x01,0x00,0x00,0xf0,0x3f,0x00,0x02,0x00,0x00,0xe0,0x1f,0x10,0x08,0x00,0x00,0x80,0x20,0x40,0x10,0x00,0x00,0x00,0x00,0x00,0x21,0x00,0x00,0x00,0x00,0x08,0x42,0x00,0x00,0x00,0x00,0x10,0x84,0x00,0x00,0x00,0x00,0x40,0x88,0x00,0x00,0x00,0x00,0x80,0x90,0x00,0x00,0x00,0x00,0x00,0x31,0x00,0x00,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x80,0x02,0x00,0x00,0x00,0xa8,0x2a,0x02,0x00,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0x00,0x40,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

    //draw all the static stuff
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_t0_13b_tr);
    u8g2.drawStr(3, 14, "Design Build Fly");
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(3, 22, "At the University of Washington");
    u8g2.drawStr(98, 63, "2025-26");
    u8g2.drawXBM(84, 15, 45, 40, image_fzx3lqe_image_bits);
    u8g2.drawStr(2, 63, "Version 0.1");
    

    //draw loading bar
    u8g2.drawRFrame(3, 28, 80, 21, 3);
    u8g2.drawRBox(3, 28, ((80-6)*loadPercent/100)+6, 21, 3); //loading bar fill in

    //draw message
    u8g2.drawStr(2, 56, message);

    u8g2.sendBuffer();

}


//////////////////////////////////////////////////////////////////////////////////////////////////
//LOAD CELL FUNCTIONS

void tareLoadCell(HX711* loadCell) { //pass a load cell object, will take the user through taring the load cell

    //prompt the user to remove load from the load cell
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_t0_16b_tr);
    u8g2.drawStr(3, 15, "Remove all load");
    u8g2.drawStr(3, 27, "from sensor.");
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(3, 44, "Press any key to continue...");
    u8g2.sendBuffer();

    pressKeyToContinue();

    //tell user we are taring
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_22b_tr);
    u8g2.drawStr(14, 39, "Taring...");
    u8g2.sendBuffer();

    loadCell->tare();
    delay(USER_NOTIF_DELAY);
}

void calibrateLoadCell(HX711* loadCell, String units) {//pass a load cell and the unit string, and will take the user through calibration
    tareLoadCell(loadCell); //start by taring

    //tell user to place known load
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_16b_tr);
    u8g2.drawStr(3, 15, "Apply a known");
    u8g2.drawStr(3, 27, "load to sensor.");
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(3, 44, "Press any key to continue...");
    u8g2.sendBuffer();

    //wait for user to acknowledge
    while (customKeypad.getKey() == NO_KEY){
        //do nothing and wait for them to press a key
    }

    long knownLoad = 0;

    String messageString = ("Enter Load " + units); //this has to be two lines to avoid a dangling pointer to the string, because
    const char* message = messageString.c_str();    //of how .c_str() works

    valueEditMenu(&knownLoad, message); //ask user to input the calibration amount

    if (knownLoad==0){ //if the user cancels, then don't calibrate
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_t0_22b_tr);
        u8g2.drawStr(10, 39, "Canceled");
        u8g2.sendBuffer();
        delay(USER_NOTIF_DELAY);
        return;
    }

    //tell user calibration is in progress
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_22b_tr);
    u8g2.drawStr(4, 40, "Calibrating...");
    u8g2.sendBuffer();

    const int N = 50; //the number of samples to average out
    long samples[N];

    for (int i = 0; i < N; i++) { //read the load cell N times and put in array
        samples[i] = loadCell->get_value();   // blocks until fresh sample. Important that it's get value, since that is with offset
        Serial.println(samples[i]);
    }

    long sum = 0; //needed to initialize for below operation
    long minVal = samples[0];
    long maxVal = samples[0];

    for (int i = 0; i < N; i++) { //finds the average, minimum, and maximum sample values of array
        long v = samples[i];
        sum += v;
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;

        /*
        Serial.println(v);
        Serial.print("Max Val: "); Serial.println(maxVal);
        Serial.print("Min Val: "); Serial.println(minVal);
        Serial.println();
        */
    }

    float avgReading = (float)sum / N; //calculates the average value

    float maxDev = maxVal-minVal; //calculates the maximum deviation
    float percentDev = abs((maxDev / avgReading) * 100.0); //calculates the percent deviation

    //set the calibration factor, this is in counts/unit load
    loadCell->set_scale(avgReading/knownLoad);
    
    Serial.print("Known Force: "); Serial.println(knownLoad);
    Serial.print("Calibrated Force: "); Serial.println(loadCell->get_units());
    Serial.print("Read Force: "); Serial.println(avgReading);
    Serial.print("Max Deviation: "); Serial.println(maxDev);

    //tell user the calibration is over
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_16b_tr);
    u8g2.drawStr(22, 13, "Calibrated");

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.setCursor(3, 24);
    u8g2.print("Raw Value: "); u8g2.print(avgReading);
    //u8g2.setCursor(3, 31);
    //u8g2.print("Calibration Factor: "); u8g2.print(avgReading/knownLoad);
    u8g2.setCursor(3, 31);
    u8g2.print("Max Sample Deviation: %"); u8g2.print(percentDev);
    u8g2.drawStr(3, 49, "Press any key to continue...");

    u8g2.sendBuffer();
    pressKeyToContinue();
}

void tareTorque(){
    tareLoadCell(&torqueSensor);
}

void tareThrust(){
    tareLoadCell(&thrustSensor);

}

void calibrateTorque(){ //helper function for the menu, calls calibrateLoadCell
    calibrateLoadCell(&torqueSensor, TRQ_UNITS);
    EEPROM.put(TRQ_CAL_ADDRESS, torqueSensor.get_scale()); //write the scale to EEPROM
}

void calibrateThrust(){//helper function for the menu, calls calibrateLoadCell
    calibrateLoadCell(&thrustSensor, THST_UNITS);
    EEPROM.put(THST_CAL_ADDRESS, thrustSensor.get_scale()); //write the scale factor to EEPROM
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//ANALOG SENSOR FUNCTIONS

float findAnalogOffset(float (*valueFunction)()){ //pass this a function that returns a value. It will take a bunch of samples and then calculate the offset from 0 and return it to you
    const int N = 30; //number of iterations
    float sum = 0;
    for (int i = 0; i < N; i++) {
        sum = sum + valueFunction();
        delay(50);
    }
    return (sum/N);
}

float getVoltage(){ //returns the average of averageCount voltage readings taken one after the other
    float sum = 0;
    for (int i = 0; i < averageCount; i++) {
        sum = sum + analogRead(VOLTAGE_PIN); //get all of the voltages
    }
    int voltage_value_in = sum/averageCount; //calculate the average voltage

    return (VOLTAGE_CALIBRATION * ((voltage_value_in * (Vcc / 1023.0))))-VOLTAGE_OFFSET; //this line converts back from analog 0-1023 to raw voltage, then subtracts off the offset and multiplies by calibration factor
}

float getCurrent(){ //returns the average of averageCount voltage readings taken one after the other
    float sum = 0;
    for (int i = 0; i < averageCount; i++){
        sum = sum + analogRead(CURRENT_PIN);
    }
    int current_value_in = sum/averageCount;    
    float current_voltage = current_value_in * (Vcc / 1023.0); //this line converts back from analog 0-1023 to raw voltage, then subtracts off the offset and multiplies by calibration factor
    return (current_voltage/CURRENT_SENSITIVITY)-CURRENT_OFFSET; //then, the analog voltage on the current pin is converted to current
}

float getAirspeed(){ 
    if(airspeedOverride != 0){ //get the set airspeed inputted by the user, if they chose an override
        return airspeedOverride;
    }
    else{ //read airspeed data from the sensor, and returning the average of a bunch of airspeed sensor readings. average count is defined globally 
        float sum = 0;
        for (int i = 0; i < averageCount; i++){
            sum = sum + analogRead(AIRSPEED_PIN);
        }
        int airspeed_value_in = sum/averageCount;
        float airspeed_voltage =  airspeed_value_in * (Vcc / 1023.0); 

        float pressure_kPa = (airspeed_voltage - zeroVoltage) / 1.0; // Convert voltage to differential pressure in kPa
        float pressure_Pa = pressure_kPa * 1000.0; // Convert kPa to Pascals

        float airspeed = 0.0;          
        if (pressure_Pa > 0) {
            airspeed = sqrt((2.0 * pressure_Pa) / airDensity); // Compute airspeed using Bernoulli equation
        }
        return airspeed;
    }
}

void zeroAnalog(){
    //tell user we are zeroizing
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_22b_tr);
    u8g2.drawStr(14, 39, "Zeroing");
    u8g2.sendBuffer();

    VOLTAGE_OFFSET = VOLTAGE_OFFSET + findAnalogOffset(getVoltage);
    CURRENT_OFFSET = CURRENT_OFFSET + findAnalogOffset(getCurrent);
}

int getRPM() { //returns RPM. Returns -1 if motor is stopped
    noInterrupts();
    long oldPulse = lastPulseMicros;
    long newPulse = currentPulseMicros;
    interrupts();
    
    Serial.print("Old Pulse: "); Serial.println(oldPulse);
    Serial.print("New Pulse: "); Serial.println(newPulse);
    unsigned long period = newPulse - oldPulse;
   
    if (period > 5e6) { //if it's been more than 5 seconds since a pulse, then return stopped
        return -1;
    }

    return 60000000.0 / (period * pulsesPerRev);
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//SENSOR READING FUNCTIONS

void resetSensorData(){ //call to reset all sensor state variables to 0
    //Sense Variables;
    testTime = 0;
    thrust = 0;
    torque = 0;
    airspeed = 0;
    current = 0;
    voltage = 0;
    RPM = 0;

    //Calculated Variables
    electricPower = 0;
    mechanicalPower = 0;
    propellerPower = 0;
    motorEfficiency = 0;
    propellerEfficiency = 0;
    systemEfficiency = 0;
}

void readSensorData(){ //call to update all of the sensor data to match most recently collected values

    if (averageGain > 100 || averageGain < 0) {
        Serial.println("Avg gain out of bounds");
        averageGain = 0;
    }

    //read RPM
    RPM = getRPM();

    //read torque and thrust if ready, otherwise keeps the old values
    if(thrustSensor.is_ready()){
        thrust = thrustSensor.get_units();
    }
    if(torqueSensor.is_ready()){
        torque = torqueSensor.get_units();
    }

    //read analog sensors
    voltage = getVoltage();
    current = (1-(averageGain/100.0))* current + (averageGain/100.0)*getCurrent(); //moving average

    //float airspeed
    airspeed = getAirspeed();

    //time
    testTime = millis()/1000.0 - testStartTime;
    
    //Calculated Variables
    electricPower = abs(voltage*current); //watts
    mechanicalPower = abs(torque*RPM*0.1047/1000); //RPM is converted to Rad/S, torque is converted to N.m from N.mm
    propellerPower = abs(thrust*airspeed/1000); //
    motorEfficiency = abs(mechanicalPower/electricPower);
    propellerEfficiency = abs(propellerEfficiency/mechanicalPower);
    systemEfficiency = abs(propellerPower/electricPower);
 
}

void displaySensorData(){//call to display all relevant test data. Needs to be passed current thrust
    u8g2.clearBuffer(); //prepare the screen for writing
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(2, 9, "Test Running..."); 
    u8g2.drawLine(0, 10, 128, 10); //draw line across bottom

    u8g2.setFont(u8g2_font_squeezed_r6_tr); //set small font for submenus

    //left bar
    u8g2.setCursor(1, 19); u8g2.print("RPM: "); u8g2.print(RPM); 
    u8g2.setCursor(1, 26); u8g2.print("THST (N): "); u8g2.print(thrust/1000); //N
    u8g2.setCursor(1, 33); u8g2.print("TRQ (N.m): "); u8g2.print(torque/1000); //Nm
    u8g2.setCursor(1, 40); u8g2.print("VLTS: "); u8g2.print(voltage);
    u8g2.setCursor(1, 47); u8g2.print("AMPS: "); u8g2.print(current); 
    u8g2.setCursor(1, 54); u8g2.print("ASPD (M/S): "); u8g2.print(airspeed); //m/s

    //right bar
    u8g2.setCursor(64, 19); u8g2.print("THRTL: %"); u8g2.print(throttle);
    u8g2.setCursor(64, 26); u8g2.print("E-PWR: "); u8g2.print(electricPower); //W
    u8g2.setCursor(64, 33); u8g2.print("MTR-PWR (W): "); u8g2.print(mechanicalPower); //W
    u8g2.setCursor(64, 40); u8g2.print("PRP-PWR (W): "); u8g2.print(propellerPower); //W
    u8g2.setCursor(64, 47); u8g2.print("MTR-EF: % "); u8g2.print(motorEfficiency);
    u8g2.setCursor(64, 54); u8g2.print("PRP-EF: % "); u8g2.print(propellerEfficiency);
    u8g2.setCursor(64, 61); u8g2.print("SYS-EF: % "); u8g2.print(systemEfficiency);

    u8g2.drawStr(1, 63, "Stop Test Any Key");
    u8g2.sendBuffer();   
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//DEBUG MENU

void debugMenu() {
    while(true){
        readSensorData();

        u8g2.clearBuffer(); //prepare the screen for writing
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(2, 9, "Debug"); 
        u8g2.drawLine(0, 10, 128, 10); //draw line across bottom
        u8g2.setFont(u8g2_font_squeezed_r6_tr); //set small font for submenus

        //left bar
        u8g2.setCursor(1, 19); u8g2.print("RPM Sensor: "); u8g2.print(digitalRead(rpmPin));
        u8g2.setCursor(1, 26); u8g2.print("THST: "); u8g2.print(thrust/1000); //N
        u8g2.setCursor(1, 33); u8g2.print("TRQ: "); u8g2.print(torque/1000); //Nm
        u8g2.setCursor(1, 40); u8g2.print("VLTS: "); u8g2.print(voltage);
        u8g2.setCursor(1, 47); u8g2.print("AMPS: "); u8g2.print(current); 
        u8g2.setCursor(1, 54); u8g2.print("ASPD: "); u8g2.print(airspeed); //m/s

        u8g2.drawStr(4, 63, "Back: *");
        u8g2.sendBuffer();    

        char userInput = customKeypad.getKey();
        // If a key is pressed, print it to the Serial Monitor
        if (userInput && userInput == '*') {
            return;
        }
    }
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//SD CARD FUNCTIONS
bool setUpTestFile(){//call this function to set up the file with the correct headers. Returns true on a successful setup.
    //ask user for test file
    valueEditMenu(&testNumber, "Enter Test Number");

    // Build filename: Test_Number_X.csv
    char filename[20];
    snprintf(filename, sizeof(filename), "Test_%d.csv", (int)testNumber); //the test name needs to be less than 8 characters before the .csv

    // Check if file already exists. If it does, prompt user to overwrite or not
    if (SD.exists(filename)) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_t0_14b_tr);
        u8g2.drawStr(2, 15, "File Name Already");
        u8g2.drawStr(2, 26, "In Use");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(3, 55, "Cancel: *");
        u8g2.drawStr(3, 47, "Overwrite: #");
        u8g2.sendBuffer();

        while(1){
            //wait for the user to press a key
            char userInput = customKeypad.getKey();
            if (userInput == '#'){
                SD.remove(filename); //delete the file
                break; //if user choses to override, exit the loop
            }
            if (userInput == '*'){
                return false; //if user picks cancel, then return false.
            }
        }
    }

    // Create and open file
    dataFile = SD.open(filename, FILE_WRITE);
    if (!dataFile) {
    Serial.println("Failed to create file!");
    return false;
    }

    Serial.print("Created file: ");
    Serial.println(filename);

    // Write CSV header
    dataFile.println("Time (s),Current (A),Voltage (V),Torque(N.mm),Thrust(mN),RPM,Airspeed(m/s),Throttle (%),Electrical Power (W),Mechanical Power (W),Propulsive Power (W),Motor Efficiency (%), Propeller Efficiency (%), System Efficiency (%)");
    dataFile.flush();   // Ensure data is written to the card

    Serial.println("Header written successfully.");
    return true; //true means it was successful
}

void writeSensorSD(){

    // Write one CSV row (Method 2: print-based)

    dataFile.print(testTime, 3);            dataFile.print(','); // float
    dataFile.print(current, 3);             dataFile.print(','); // float
    dataFile.print(voltage, 3);             dataFile.print(','); // float
    dataFile.print(torque, 3);              dataFile.print(','); // float
    dataFile.print(thrust, 3);              dataFile.print(','); // float
    dataFile.print(RPM);                    dataFile.print(','); // int
    dataFile.print(airspeed, 3);            dataFile.print(','); // float
    dataFile.print(throttle);               dataFile.print(','); // int
    dataFile.print(electricPower, 3);       dataFile.print(','); // float
    dataFile.print(mechanicalPower, 3);     dataFile.print(','); // float
    dataFile.print(propellerPower, 3);      dataFile.print(','); // float
    dataFile.print(motorEfficiency, 3);     dataFile.print(','); // float
    dataFile.print(propellerEfficiency, 3); dataFile.print(','); // float
    dataFile.print(systemEfficiency, 3);    dataFile.println();  // float + newline

    //don't flush all the time
    if ((millis()-lastFlush) > flushPeriodMillis){
        dataFile.flush();
        lastFlush = millis();
        Serial.println("Flushed Data");
    }
    return;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//TEST FUNCTIONS

void setThrottle(int throttleSetting){ //pass this a throttle from 0-100 and it will safely write it to the ESC
    int throttleMicroseconds = ((throttleSetting/100.0)*(MAX_THROTTLE-MIN_THROTTLE)+MIN_THROTTLE);

    if (throttleSetting > 100 || throttleSetting < 0){ //if the throttle is out of bounds, set it to 0
        throttleMicroseconds = MIN_THROTTLE;
    }
    
    esc.writeMicroseconds(throttleMicroseconds);
    Serial.print("Throttle Microseconds: "); Serial.println(throttleMicroseconds);
}

void runSmoothRampTest(){ //give time in millis since starting the test, returns a struct containing info about throttle settings and whether to record data
    esc.writeMicroseconds(MIN_THROTTLE);

    if(!setUpTestFile()){
        return;
    }

    wdt_enable(WDTO_2S); //this is the watchdog timer. If it goes 2s without wdt_reset being called, the board will do a hardware reset.
    wdt_reset();

    resetSensorData(); //this line makes sure that if a sensor is missing, it shows as zero and not the value of the last test
    
    //initialize the test variables
    bool testRunning = true;
    throttle = 0;
    long startTime = millis(); //this is for keeping track of what throttle level to set
    long time = startTime;
    testStartTime = millis()/1000; //this is for recording time to the SD card in seconds

    while(testRunning){
        wdt_reset(); //pet that dawg! (cause you're keeping the watchdog from going off by resetting every loop)
        time = millis() - startTime;

        //throttle mapping
        if (time < rampTime*1000){ //if time is in initial ramp up period
            Serial.println(time/1000);
            throttle = 100*(time/(rampTime))/1000; 

        } else if ((time >= rampTime*1000) & (time < (rampTime + topTime)*1000)){ //if time is at the top
            throttle = 100;

        } else if (time >= (rampTime + topTime)*1000){ //if time is past the top
            long timeAfterRampDown = time-(rampTime + topTime) * 1000;
            throttle = 100-100*(timeAfterRampDown/(rampTime))/1000;
        }

        //read, display, and record sensor data for user
        readSensorData();
        displaySensorData();
        writeSensorSD();

        //detect the end of the test
        if (time > (rampTime*2 + topTime)*1000) {
            testRunning = false;
        }

        setThrottle(throttle);

        while ((millis() - (time+startTime)) < testDataInterval) { //don't update again until after millis time has passed. Just check for E-Stop
            Serial.println("Waiting for next loop");
            char userInput = customKeypad.getKey();
            if (userInput){
                throttle = 0;
                setThrottle(0);
                return;
            }
        }
        //
    }

    throttle = 0;
    setThrottle(0);
    testNumber++;
    dataFile.close();
    wdt_disable(); //turn off the watch dog
}

void runTest(){//this method is in charge of deciding which test to run and then running it
    runSmoothRampTest();
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//RUNTIME FUNCTIONS

void setup() {
    u8g2.begin();
    Serial.begin(9600); // Start serial communication
    Serial.println("Keypad Ready");

    drawLoadingScreen(0, "Attaching pins");
    attachInterrupt(digitalPinToInterrupt(rpmPin), rpmISR, RISING); //attach RPM pin
    esc.attach(ESC_PIN);
    esc.writeMicroseconds(MIN_THROTTLE);

    drawLoadingScreen(10, "Initializing SD-Card");
    // Required for Mega SPI
    pinMode(53, OUTPUT);
    pinMode(SD_CS_PIN, OUTPUT);

    while (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
    }

    drawLoadingScreen(20, "Force Sensor Initialization");
    torqueSensor.begin(TRQ_DOUT, TRQ_CLK);
    torqueSensor.set_gain(128);

    thrustSensor.begin(THST_DOUT, THST_CLK);
    torqueSensor.set_gain(128);

    drawLoadingScreen(30, "Thrust Sensors Zeroing");
    torqueSensor.tare();
    thrustSensor.tare();

    drawLoadingScreen(40, "Analog Zeroing");
    //zeroAnalog(); skipping this currently

    drawLoadingScreen(50, "Loading Calibration Factors");
    float torqueSensorScale;
    EEPROM.get(TRQ_CAL_ADDRESS, torqueSensorScale); 
    torqueSensor.set_scale(torqueSensorScale);

    float thrustSensorScale;
    EEPROM.get(THST_CAL_ADDRESS, thrustSensorScale); 
    Serial.println(thrustSensorScale);
    thrustSensor.set_scale(thrustSensorScale);


}

//loop draws a menu and allows for navigation. Once something is selected, it does that function, then continues looping. 
//If you would like your function to return to the main menu after completing, set the currentMenuId to zero at the end of your function runs
void loop() { 
    drawMenu(currentMenuId);
    
    //wait for the user to press a key
    char userInput = customKeypad.getKey();
    
    // If a key is pressed, print it to the Serial Monitor
    if (userInput) {
        Serial.println(userInput);

        //check to see if the key is a number, if it is then save the number as "value"
        if (userInput >= '0' && userInput <= '9') {
            int choice = userInput - '0';
            choice = getChosenMenuId(choice); //this turns choice from the number option of 1-9 that the user picked, to the id of the chosen menu. It will return -1 if invalid
            Serial.println(choice);

            //if the choice is valid, execute that menu item
            if (choice != -1){
                executeMenu(choice);
            }
        
        //asterisk is the back button
        } else if (userInput == '*') {
            Serial.println("Go back");
            if (currentMenuId == 0){ //If a go back from the main menu is triggered, do nothing
                //setup();
            } else {
                currentMenuId = getMenu(currentMenuId)->parentId; //if the user presses the back button, go back to the parent.
            }

        }

        Serial.print("Active ID is now: ");
        Serial.println(currentMenuId);

    } 
}
