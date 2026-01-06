#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

//-----------------------------------------MENU FUNCTIONS--------------------------------------
/*
MENU FLOW CHART:
MAIN MENU
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

/*
enum ItemType { SUBMENU, TOGGLE, VALUE, ACTION };

//this defines the different types of menu items you can have.
struct MenuItem {
    int itemId;         // For this submenu
    const char* label;
    ItemType type;
    int parentId;       // To allow for parent menus
    void* variable;     // Pointer to the bool or float
    void (*action)();   // For ACTION or DEBUG screens
};

MenuItem menus[] = {
    {1, "Run Test", ACTION, 0, NULL, runTest}
    {2, "Configure Test", SUBMENU, 0, NULL, NULL},
    {21, "Test Profile Selection", SUBMENU, 2, NULL, NULL},
};
*/

// SSD1309, 128x64, I2C
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);

void setup() {
    u8g2.begin();
}

void loop() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "SSD1309 Works!");
    u8g2.sendBuffer();
    delay(1000);
}
