#include <Arduino.h>
#include <header.cpp>

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
    for (int i = 0; i < menuCount; i++) {
        if (menus[i].parentId == menuId) {
            MenuItem* subMenu = getMenu(menus[i].itemId); //grab the menu item pointer (this is like super bad and slow but it works!)

            u8g2.setCursor(4, 12 + menusDrawn*menuOffset);
            u8g2.print(menusDrawn); //this prints the option number
            u8g2.drawStr(12, 12 + menusDrawn*menuOffset, subMenu->label); //this prints the name of the menu

            if (subMenu->type == TYPE_VALUE) { //if the value exists and isn't a null pointer, print it at the end
                Serial.print("Menu item is a value");
                if (subMenu->variable != nullptr){
                    Serial.print("Menu item has value assigned");
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

void valueEditMenu(long* value, const char* label){ //pass this method a pointer to an int and a label to show for the int. It will give the user the UI to type in any positive integer of 8 digits or less.
    Serial.println("Inside value edit menu!");
    if (!value){
        Serial.println("value doesn't exist, returning");
        return;
    }

    int startTime = millis(); //we track how long since it started so that we can time out
    Serial.print("Value is: "); Serial.println(*value);

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
            //targetMenu.action(); //all the function if it's a function menu item
    } else if (targetMenu.type == TYPE_VALUE) {
        Serial.println("Value Type!");
        valueEditMenu(targetMenu.variable, targetMenu.label);
    } else if (targetMenu.type == TYPE_TOGGLE) {
        Serial.println("Toggle Type!");
        //write bool change function here
    }
}

//pass load percent as an int from 0-100
void drawLoadingScreen(int loadPercent){
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

void setup() {
    u8g2.begin();
    Serial.begin(9600); // Start serial communication
    Serial.println("Keypad Ready");

    drawLoadingScreen(0);
    Serial.println("Force Sensor Initialization");
    torqueSensor.begin(TRQ_DOUT, TRQ_CLK);
    thrustSensor.begin(THST_DOUT, THST_CLK);

    for (int i = 0; i < 20; i++){
        
    }
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
            if (currentMenuId == 0){ //If a go back from the main menu is triggered, run setup again
                setup();
            } else {
                currentMenuId = getMenu(currentMenuId)->parentId; //if the user presses the back button, go back to the parent.
            }

        }

        Serial.print("Active ID is now: ");
        Serial.println(currentMenuId);

    } 
}
