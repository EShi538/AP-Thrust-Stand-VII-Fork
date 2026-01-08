#include <Arduino.h>
#include <Keypad.h>
#include <U8g2lib.h>
#include <Wire.h>

//-----------------------------------------GLOBAL VARIABLES-----------------------------------

//test setup
int rpmMarkers = 1;
int testNumber = 1;
//smooth ramp
int rampTime = 30; //in seconds
int topTime = 4; //in seconds
int smoothThrottleMax = 100; //as a percent
bool upDown = true; //if true, go up and then back down


//---------------------------------------KEYBOARD SETUP---------------------------------------
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

// Define the key mapping based on your keypad's layout
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Connect the row pins to the Arduino digital pins
byte rowPins[ROWS] = {9, 8, 7, 6}; // Adjust these if your wiring is different

// Connect the column pins to the Arduino digital pins
byte colPins[COLS] = {5, 4, 3, 2}; // Adjust these if your wiring is different

// Create the Keypad object
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);


//---------------------------------------SCREEN AND MENU--------------------------------------
//this is the magic that makes the screen work. Screen needs to be hooked up to SDA and SCL
// SSD1309, 128x64, I2C
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);

//UW Logo
static const unsigned char image_download_bits[] = {0xa0,0x00,0x00,0x00,0x00,0x80,0x07,0x00,0x00,0x00,0x04,0x3e,0x00,0x00,0x00,0xf8,0xf8,0x00,0x00,0x00,0xc0,0xe3,0x43,0x01,0x00,0x0f,0xdf,0xff,0x03,0x00,0x7c,0xfc,0xff,0x01,0x00,0xe0,0xf9,0xff,0x01,0x00,0x80,0xef,0x7f,0x02,0x00,0x00,0xff,0x3f,0x00,0x00,0x00,0xfe,0x7f,0x40,0x00,0x00,0xfc,0x7f,0x80,0x00,0x00,0xf8,0x07,0x04,0x02,0x00,0x20,0x08,0x10,0x04,0x00,0x00,0x00,0x40,0x08,0x00,0x00,0x00,0x82,0x10,0x00,0x00,0x00,0x04,0x21,0x00,0x00,0x00,0x10,0x22,0x00,0x00,0x00,0x20,0x24,0x00,0x00,0x00,0x40,0x0c,0x00,0x00,0x00,0xc0,0x07,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0xa0,0x00,0x00,0x00,0xaa,0x8a,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0xd0,0x03,0x00};


//-----------------------------------------MENU FUNCTIONS--------------------------------------
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



enum ItemType { TYPE_SUBMENU, TYPE_TOGGLE, TYPE_VALUE, TYPE_ACTION };

struct MenuItem {
    int itemId;
    const char* label;
    ItemType type;
    int parentId;
    void* variable;
    void (*action)();
};

// Use the renamed enum TYPE_ACTION to avoid conflicts
MenuItem menus[] = {
    //MAIN MENU
    {0, "Main Menu", TYPE_SUBMENU, 1000, NULL, NULL},

    {1, "Run Test", TYPE_ACTION, 0, NULL, NULL},

    {2, "Configure Test", TYPE_SUBMENU, 0, NULL, NULL},
        {21, "Select Test Profile", TYPE_ACTION, 2, NULL, NULL},
        {22, "Configure Test Profiles", TYPE_SUBMENU, 2, NULL, NULL},
            {221, "Smooth Ramp", TYPE_SUBMENU, 22, NULL, NULL},
                {2211, "Ramp Time", TYPE_VALUE, 22, &rampTime, NULL},
                {2212, "Top Time", TYPE_VALUE, 22,},
            {222, "Interval Ramp", TYPE_SUBMENU, 22, NULL, NULL},
        {23, "Test Setup Selection", TYPE_SUBMENU, 2, NULL, NULL},
            {231, "RPM Marker Count", TYPE_VALUE, 23, NULL, NULL},

    {3, "Tare Sensors", TYPE_SUBMENU, 0, NULL, NULL},
        {31, "Zero All", TYPE_ACTION, 3, NULL, NULL},
        {32, "Tare Torque", TYPE_ACTION, 3, NULL, NULL},
        {33, "Tare Thrust", TYPE_ACTION, 3, NULL, NULL},
        {34, "Zero Force", TYPE_ACTION, 3, NULL, NULL},
        {35, "Zero Torque", TYPE_ACTION, 3, NULL, NULL},
        {36, "Zero Analog", TYPE_ACTION, 3, NULL, NULL},

    {4, "Debug", TYPE_ACTION, 0, NULL, NULL},

    //TARE SENSORS


    //Configure Test

}; 

//this has to exist because calculating the number of items
//in a struct datatype is hard I guess
int menuCount = sizeof(menus)/sizeof(menus[0]);
int currentMenuId = 0; //this keeps track of the current menu state
//returns null if no Menu Item with that ID, otherwise returns a pointer
//to the item.


MenuItem* getMenu(int menuId) {
    for (int i = 0; i < menuCount; i++) {
        if (menus[i].itemId == menuId) {
            return &menus[i]; //this is a pointer to the menu item.
        }
    }
    return nullptr; //returns null if there's no menu with that ID
}


void drawMenu(int menuId) {
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
    for (int i = 0; i < menuCount; i++) {
        if (menus[i].parentId == menuId) {
            MenuItem* subMenu = getMenu(menus[i].itemId); //grab the menu item pointer (this is like super bad and slow but it works!)

            u8g2.setCursor(4, 12 + menusDrawn*menuOffset);
            u8g2.print(menusDrawn);
            u8g2.drawStr(12, 12 + menusDrawn*menuOffset, subMenu->label);

            menusDrawn++; //increment the counter so we draw the next one lower
        }
    }
    u8g2.sendBuffer();
}

int getChosenMenuId(int choice) { //given an the int of the choice (1 index), then it will find the chosen menu Id. If the choice is invalid, then returns -1

    //fetch that menus pointer
    for (int i = 0; i < menuCount; i++) {
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

void executeMenu(int targetMenuId) {
    MenuItem targetMenu = menus[targetMenuId];
    if (targetMenu.type == TYPE_SUBMENU) {
        currentMenuId = targetMenuId; //navigate the submenu if it's a submenu item
    } else if (targetMenu.type == TYPE_ACTION) {
        targetMenu.action(); //call the function if it's a function menu item
    } else if (targetMenu.type == TYPE_VALUE) {
        //write value change function here
    } else if (targetMenu.type == TYPE_TOGGLE) {
        //write bool change function here
    }
}

//pass load percent as an int from 0-100
void drawLoadingScreen(int loadPercent){
    u8g2.clearBuffer();

    u8g2.drawXBM(85, 25, 38, 26, image_download_bits);

    u8g2.setFont(u8g2_font_t0_13b_tr);
    u8g2.drawStr(3, 14, "Design Build Fly");

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(3, 22, "At the University of Washington");

    u8g2.drawStr(90, 60, "2025-26");

    u8g2.drawRFrame(2, 30, 80, 21, 3); //loading bar frame

    u8g2.drawRBox(2, 30, ((80-6)*loadPercent/100)+6, 21, 3); //loading bar fill in

    u8g2.drawStr(2, 60, "Version 0.1");

    u8g2.sendBuffer();

}

void setup() {
    u8g2.begin();
    for (int i = 0; i < 20; i++){
        drawLoadingScreen(i*5);
    }
}


//loop draws a menu and allows for navigation. Once something is selected, it does that function, then continues looping. 
//If you would like your function to return to the main menu after completing, set the currentMenuId to zero at the end of your function runs
void loop() { 
    drawMenu(currentMenuId);
    delay(5000);
    drawLoadingScreen(0);
    delay(5000);
    //executeMenu(3);
    /*int choice = 3; //get keypad input here
    int chosenId = getChosenMenuId(choice);
    if (chosenId) {
        executeMenu(chosenId);
    }*/
    
}
