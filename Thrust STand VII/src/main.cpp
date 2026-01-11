#include <Arduino.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HX711.h>
#include <Servo.h>

/*TODO: 
Thrust Profiles
Airspeed
SD Card Reading
EEPROM Calibration
RPM Verification
Current and Voltage
E-Stop
*/
//////////////////////////////////////////////////////////////////////////////////////////////////
//FUNCTION EXTERNALS
extern void debugMenu();
extern void runTest();
//////////////////////////////////////////////////////////////////////////////////////////////////
//LOAD CELLS

HX711 thrustSensor;
HX711 torqueSensor;

#define TRQ_DOUT 52
#define TRQ_CLK 53
#define TRQ_UNITS "(N.mm)"

#define THST_DOUT 50
#define THST_CLK 51
#define THST_UNITS "(mN)"

extern void tareTorque(); //these need to be here so the menu structure knows these exist before they're declared in the file
extern void calibrateTorque();

extern void tareThrust();
extern void calibrateThrust();

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

////////////////////////////////////////////////////////////////////////////////////////
//THROTTLE LOGIC DEFINITIONS
//(For a HARGRAVE MICRODRIVE ESC, accepted PWM frequencies range from 50Hz to 499 Hz

Servo esc; 
const int MIN_THROTTLE = 1000;
const int MAX_THROTTLE = 2000;
const int ESC_PIN = 2;

//-----------------------------------------GLOBAL VARIABLES-----------------------------------

//UI
#define USER_NOTIF_DELAY 1800

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
            {232, "Airspeed Override (m/s)", TYPE_VALUE, 23, &pulsesPerRev, NULL},


    {3, "Tare Sensors", TYPE_SUBMENU, 0, NULL, NULL},
        {31, "Zero All", TYPE_ACTION, 3, NULL, NULL},
        {32, "Zero Thrust", TYPE_ACTION, 3, NULL, tareThrust},
        {33, "Zero Torque", TYPE_ACTION, 3, NULL, tareTorque},
        {34, "Calibrate Thrust Sensor", TYPE_ACTION, 3, NULL, calibrateThrust},
        {35, "Calibrate Torque Sensor", TYPE_ACTION, 3, NULL, calibrateTorque},
        {36, "Zero Analog", TYPE_ACTION, 3, NULL, NULL},

    {4, "Debug", TYPE_ACTION, 0, NULL, debugMenu},
}; 

//this has to exist because calculating the number of items
//in a struct datatype is hard I guess
const int MENU_COUNT = sizeof(menus)/sizeof(menus[0]);
int currentMenuId = 0; //this keeps track of the current menu state

//returns null if no Menu Item with that ID, otherwise returns a pointer
//to the item.
MenuItem* getMenu(int menuId) {
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

void drawLoadingScreen(int loadPercent){//pass load percent as an int from 0-100
    u8g2.clearBuffer();

    //UW Logo
    //static const unsigned char image_download_bits[] = {0xa0,0x00,0x00,0x00,0x00,0x80,0x07,0x00,0x00,0x00,0x04,0x3e,0x00,0x00,0x00,0xf8,0xf8,0x00,0x00,0x00,0xc0,0xe3,0x43,0x01,0x00,0x0f,0xdf,0xff,0x03,0x00,0x7c,0xfc,0xff,0x01,0x00,0xe0,0xf9,0xff,0x01,0x00,0x80,0xef,0x7f,0x02,0x00,0x00,0xff,0x3f,0x00,0x00,0x00,0xfe,0x7f,0x40,0x00,0x00,0xfc,0x7f,0x80,0x00,0x00,0xf8,0x07,0x04,0x02,0x00,0x20,0x08,0x10,0x04,0x00,0x00,0x00,0x40,0x08,0x00,0x00,0x00,0x82,0x10,0x00,0x00,0x00,0x04,0x21,0x00,0x00,0x00,0x10,0x22,0x00,0x00,0x00,0x20,0x24,0x00,0x00,0x00,0x40,0x0c,0x00,0x00,0x00,0xc0,0x07,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0xa0,0x00,0x00,0x00,0xaa,0x8a,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0xd0,0x03,0x00};
    //u8g2.drawXBM(85, 25, 38, 26, image_download_bits);

    u8g2.setFont(u8g2_font_t0_13b_tr);
    u8g2.drawStr(3, 14, "Design Build Fly");

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(3, 22, "At the University of Washington");

    u8g2.drawStr(90, 60, "2025-26");

    u8g2.drawRFrame(2, 30, 126, 21, 3); //loading bar frame

    u8g2.drawRBox(2, 30, ((126-6)*loadPercent/100)+6, 21, 3); //loading bar fill in

    u8g2.drawStr(2, 60, "Version 0.1");


    u8g2.sendBuffer();

}

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

void tareTorque(){
    tareLoadCell(&torqueSensor);
}

void tareThrust(){
    tareLoadCell(&thrustSensor);
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
    u8g2.setCursor(3, 31);
    u8g2.print("Calibration Factor: "); u8g2.print(avgReading/knownLoad);
    u8g2.setCursor(3, 38);
    u8g2.print("Max Sample Deviation: %"); u8g2.print(percentDev);
    u8g2.drawStr(3, 49, "Press any key to continue...");

    u8g2.sendBuffer();
    pressKeyToContinue();
}

void calibrateTorque(){ //helper function for the menu, calls calibrateLoadCell
    calibrateLoadCell(&torqueSensor, TRQ_UNITS);
}

void calibrateThrust(){//helper function for the menu, calls calibrateLoadCell
    calibrateLoadCell(&thrustSensor, THST_UNITS);
}

int getRPM() { //returns RPM. Returns -1 if motor is stopped
    noInterrupts();
    long oldPulse = lastPulseMicros;
    long newPulse = currentPulseMicros;
    unsigned long period = oldPulse - newPulse;
    interrupts();

    if (period > 5e6) { //if it's been more than 5 seconds since a pulse, then return stopped
        return -1;
    }

    return 60000000.0 / (period * pulsesPerRev);
}

struct testExecuteInfo {
    int throttle; //as a percent from 0-100
    bool collectData;
    bool testComplete;
};

testExecuteInfo smoothRamp(int time){ //give time in millis since starting the test, returns a struct containing info about throttle settings and whether to record data
    testExecuteInfo info;
    info.collectData = true;

    //control throttle
    info.throttle = 0;
    if (time < rampTime*1000){ //if time is in initial ramp up period
        info.throttle = (100*(rampTime*1000-time));
    } else if (time >= rampTime*1000 & time < (rampTime + topTime)*1000){ //if time is at the top
        info.throttle = 100;
    } else if (time >= (rampTime + topTime)*1000){ //if time is past the top
        info.throttle = 100-(100*((2*rampTime+topTime)*1000-time));
    }

    //control test completion. Defaults to stop the test (in case of errors)
    info.testComplete = true;
    if (time < (2*rampTime + topTime)*1000){
        info.testComplete = false; 
    }

    return info;
}

void runTest(){
    //initialize variables
    float thrust = 0;
    float torque = 0;
    float airspeed = 0;
    int RPM = 0;

    while (runTest){

        //read RPM
        int RPM = getRPM();

        //read torque and thrust
        if(thrustSensor.is_ready()){
            float thrust = thrustSensor.get_units();
        }
        if(torqueSensor.is_ready()){
            float torque = torqueSensor.get_units();
        }
        
        //float airspeed

        u8g2.clearBuffer(); //prepare the screen for writing
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(2, 9, "Test Running..."); 
        u8g2.drawLine(0, 10, 128, 10); //draw line across bottom

        u8g2.setFont(u8g2_font_squeezed_r6_tr); //set small font for submenus

        //left bar
        u8g2.setCursor(1, 19); u8g2.print("RPM: "); u8g2.print(RPM); 
        u8g2.setCursor(1, 26); u8g2.print("THST: "); u8g2.print(thrust/1000); //N
        u8g2.setCursor(1, 33); u8g2.print("TRQ: "); u8g2.print(torque/1000); //Nm
        u8g2.setCursor(1, 40); u8g2.print("VLTS: "); u8g2.print(torque);
        u8g2.setCursor(1, 47); u8g2.print("AMPS: "); u8g2.print(torque); 
        u8g2.setCursor(1, 54); u8g2.print("ASPD: "); u8g2.print(torque); //m/s

        //right bar
        u8g2.setCursor(64, 19); u8g2.print("THRTL: %"); u8g2.print(digitalRead(rpmPin));
        u8g2.setCursor(64, 26); u8g2.print("E-PWR: "); u8g2.print(thrust/1000); //W
        u8g2.setCursor(64, 33); u8g2.print("MTR-PWR: "); u8g2.print(torque/1000); //W
        u8g2.setCursor(64, 40); u8g2.print("PRP-PWR: "); u8g2.print(torque); //W
        u8g2.setCursor(64, 47); u8g2.print("MTR-EF: %"); u8g2.print(torque);
        u8g2.setCursor(64, 54); u8g2.print("PRP-EF: %"); u8g2.print(torque);


        u8g2.drawStr(4, 63, "Stop Test *");
        u8g2.sendBuffer();    

        pressKeyToContinue();
    }

}

void debugMenu() {
    while(true){
        u8g2.clearBuffer(); //prepare the screen for writing
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(2, 9, "Debug"); 
        u8g2.drawLine(0, 10, 128, 10); //draw line across bottom

        u8g2.setFont(u8g2_font_squeezed_r6_tr); //set small font for submenus

        u8g2.setCursor(4, 19); u8g2.print("RPM Sensor: "); u8g2.print(digitalRead(rpmPin));
        u8g2.setCursor(4, 26); u8g2.print("Thrust Value: "); u8g2.print(thrustSensor.get_units(1));
        u8g2.setCursor(4, 33); u8g2.print("Torque Value: "); u8g2.print(torqueSensor.get_units(1));

        u8g2.drawStr(4, 63, "Back: *");
        u8g2.sendBuffer();    

        char userInput = customKeypad.getKey();
        // If a key is pressed, print it to the Serial Monitor
        if (userInput && userInput == '*') {
            return;
        }
    }
}

void setup() {
    u8g2.begin();
    Serial.begin(9600); // Start serial communication
    Serial.println("Keypad Ready");

    drawLoadingScreen(0);
    Serial.println("Force Sensor Initialization");
    torqueSensor.begin(TRQ_DOUT, TRQ_CLK);
    torqueSensor.set_gain(128);

    thrustSensor.begin(THST_DOUT, THST_CLK);
    torqueSensor.set_gain(128);

    drawLoadingScreen(10);
    Serial.println("Force Sensors Zeroizing");
    torqueSensor.tare();
    drawLoadingScreen(20);
    thrustSensor.tare();

    drawLoadingScreen(30);

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
