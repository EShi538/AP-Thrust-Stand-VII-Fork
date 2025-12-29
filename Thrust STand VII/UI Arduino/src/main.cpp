#include <Arduino.h>

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

void runTest() {
    serial.print("Test running")
}
void setup() {

}

void loop() {
}
