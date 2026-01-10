#include <Arduino.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HX711.h>
#include <Servo.h>

//////////////////////////////////////////////////////////////////////////////////////////////////
//LOAD CELLS

HX711 thrustSensor;
HX711 torqueSensor;

#define TRQ_DOUT 52
#define TRQ_CLK 53

#define THST_DOUT 50
#define THST_CLK 51

////////////////////////////////////////////////////////////////////////////////////////
//THROTTLE LOGIC DEFINITIONS
//(For a HARGRAVE MICRODRIVE ESC, accepted PWM frequencies range from 50Hz to 499 Hz

Servo esc; 

const int MIN_THROTTLE = 1000;

const int ESC_MIN = 1000;
const int ESC_MAX = 2000;

const int ESC_PIN = 3;
//-----------------------------------------GLOBAL VARIABLES-----------------------------------

//test setup
long rpmMarkers = 1;
long testNumber = 1;
//smooth ramp
long rampTime = 30; //in seconds
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

    {1, "Run Test", TYPE_ACTION, 0, NULL, NULL},
    
    {2, "Configure Test", TYPE_SUBMENU, 0, NULL, NULL},
        {21, "Select Profile", TYPE_ACTION, 2, NULL, NULL},
        {22, "Configure Profiles", TYPE_SUBMENU, 2, NULL, NULL},
            {221, "Smooth", TYPE_SUBMENU, 22, NULL, NULL},
                {2211, "Ramp Up Time (s)", TYPE_VALUE, 221, &rampTime, NULL},
                {2212, "Top Hold Time (s)", TYPE_VALUE, 221, &topTime, NULL},
            {222, "Intervals", TYPE_SUBMENU, 22, NULL, NULL},
        {23, "Configure Hardware", TYPE_SUBMENU, 2, NULL, NULL},
            {231, "RPM Marker Count", TYPE_VALUE, 23, &rpmMarkers, NULL},

    {3, "Tare Sensors", TYPE_SUBMENU, 0, NULL, NULL},
        {31, "Zero All", TYPE_ACTION, 3, NULL, NULL},
        {32, "Tare Torque", TYPE_ACTION, 3, NULL, NULL},
        {33, "Tare Thrust", TYPE_ACTION, 3, NULL, NULL},
        {34, "Zero Force", TYPE_ACTION, 3, NULL, NULL},
        {35, "Zero Torque", TYPE_ACTION, 3, NULL, NULL},
        {36, "Zero Analog", TYPE_ACTION, 3, NULL, NULL},

    {4, "Debug", TYPE_ACTION, 0, NULL, NULL},
}; 

