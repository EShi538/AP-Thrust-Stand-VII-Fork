#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

//-----------------------------------------MENU FUNCTIONS--------------------------------------
/*
MENU FLOW CHART:
0 MAIN MENU
    1 Run Test
        Test Confirmation Info Screen
        OK Button
        Test Data Readout (of all sensors)

    2 Configure Test
        21 Test Profile Selection
            211 Smooth Up/Down
                2111 Total Time
                2112 Time at Top
                2113 Max Throttle
                2114 Up/Down Mode (Just up or up and down)
            212 Discreet Intervals
                2121 Interval Count
                2122 Interval Time
                2123 Max Throttle
                2124 Up/Down Mode (Just up or up and down)
        22 Test Setup Selection
            221 RPM Marker Count
            222 Test File Name

    3 Tare Sensors
        // Tare Torque
        // Tare Force
        // Zero All
        // Zero Force
        // Zero Torque
        // Zero Airspeed
        // Zero Current
        // Zero Voltage

    4 Debug Menu
        Display read values for all sensors,
*/

//this is the magic that makes the screen work. Screen needs to be hooked up to SDA and SCL
// SSD1309, 128x64, I2C
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);

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
    {3, "Tare Sensors", TYPE_SUBMENU, 0, NULL, NULL},
    {4, "Debug", TYPE_ACTION, 0, NULL, NULL},

    //TARE SENSORS
    {31, "Zero All", TYPE_ACTION, 3, NULL, NULL},
    {32, "Tare Torque", TYPE_ACTION, 3, NULL, NULL},
    {33, "Tare Thrust", TYPE_ACTION, 3, NULL, NULL},
    {34, "Zero Forces", TYPE_ACTION, 3, NULL, NULL},
    {35, "Zero Analog", TYPE_ACTION, 3, NULL, NULL},
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

int getChosenMenuId(int choice) { //given an the int of the choice (1 index), then it will find the chosen menu Id. If the choice is invalid, then returns null

    //fetch that menus pointer
    for (int i = 0; i < menuCount; i++) {
        if (menus[i].parentId == currentMenuId) {
            if (choice == 1) {
                return menus[i].itemId;
            } else if (choice < 1){
                return NULL;
            } else {
                choice = choice - 1;
            }
        }
    }
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

void setup() {
    u8g2.begin();
}


//loop draws a menu and allows for navigation. Once something is selected, it does that function, then continues looping. 
//If you would like your function to return to the main menu after completing, set the currentMenuId to zero at the end of your function runs
void loop() { 
    drawMenu(currentMenuId);
    int choice = 3; //get keypad input here
    int chosenId = getChosenMenuId(choice);
    if (chosenId) {
        executeMenu(chosenId);
    }
}
