#include <Wire.h> // I2C communication
#include <DIYables_IRcontroller.h> // DIYables_IRcontroller library
#include <LiquidCrystal_I2C.h> // For the LCD display
#include <RTClib.h> // Library for the RTC module
#include <AccelStepper.h> // Include the AccelStepper Library for the stepper motor

/* Initialize constants */
#define IR_RECEIVER_PIN 7 // The Arduino pin connected to IR controller
#define FULL_STEP 4
#define STEP_PER_REVOLUTION 2048 // this value is from datasheet

/* Initialize objects */
RTC_DS3231 rtc;                                              // RTC time module
AccelStepper stepper(FULL_STEP, 11, 9, 10, 8);               // Stepper motor
DIYables_IRcontroller_17 irController(IR_RECEIVER_PIN, 200); // IR controller, debounce 200ms
LiquidCrystal_I2C lcd(0x27, 20, 4);                          // LCD display

/* Display variables */
bool displayOn = true;
bool menuDrawn = false;

/* Menu variables */
int selectedIndex = 0;
int scrollOffset = 0;
const char* menuItems[] = {"Feed Now", "Set Schedule", "Set Date and Time", "See Time"};
const int menuItemCount = 4;

// create array to convert digit days to words
const char dayInWords[7][4]    = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
// create array to convert digit months to words
const char monthInWords[13][4] = {" ","JAN","FEB","MAR","APR","MAY","JUN",
                                   "JUL","AUG","SEP","OCT","NOV","DEC"};

/* Automatic feeding variables */
int  scheduledHour = 8;
int  scheduledMinute = 0;
bool scheduleEnabled = false;
byte scheduleDays = 0;   // bitmask: bit0=Sun, bit1=Mon, ... bit6=Sat
bool scheduleFedThisMinute = false;

/* Stepper motor variable */
bool motorTriggered = false;

// state variable for keeping track of modes
enum AppState { STATE_MENU, STATE_FEEDING, STATE_SCHEDULE, STATE_CLOCK, STATE_TIME };
AppState appState = STATE_MENU; // initialize initial state

/* Clock set variables */
enum ClockSetField { CS_YEAR, CS_MONTH, CS_DAY, CS_HOUR, CS_MIN, CS_SEC, CS_CONFIRM };
ClockSetField csField = CS_YEAR; 
int  csValues[6]; // year, month, day, hour, minute, second
bool clockSetDrawn = false; // bool to control drawing the clock

// arrays for clock display
const char* csLabels[] = {"Year","Month","Day","Hour","Min","Sec"};
const int csMin[] = {2000, 1, 1, 0,  0,  0};
const int csMax[] = {2099,12,31,23, 59, 59};

/* Schedule sub-state variables */
enum SchedStep { SS_HOUR, SS_MIN, SS_DAYS, SS_SAVE};
SchedStep schedStep = SS_HOUR; // Set initial state to hour (what we always set first)
int  editHour = 8;
int  editMinute = 0;
byte editDays = 0;
int  dayCursor = 0; // offset - current day being highlighted on schedule menu 
bool schedDrawn = false; // Bool to control drawing the schedule menu

void setup() {
  Serial.begin(9600);

  // setup RTC module
  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1);
  }

  // begin IR remote
  irController.begin();

  // stepper motor setup
  stepper.setMaxSpeed(500.0);
  stepper.setAcceleration(300.0);
  stepper.setCurrentPosition(0);

  // starting LCD screen
  lcd.init();
  lcd.begin(20, 4);
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(F("Starting Automatic"));
  lcd.setCursor(0, 1); lcd.print(F("Fish Feeder..."));

  delay(3000);
  lcd.clear();
}

// function that toggles the display on and off
void toggle_display() {
  // Uses a boolean variable to determine when the display should be ON or OFF
  if (displayOn) {
    lcd.noDisplay();
    lcd.noBacklight();
    displayOn = false;
  } else {
    lcd.display();
    lcd.backlight();
    displayOn = true;
    menuDrawn = false;
  }
}

void draw_menu() {
  lcd.clear();

  // draw up to 4 visible rows
  for (int row = 0; row < 4; row++) {
    int idx = scrollOffset + row;
    if (idx >= menuItemCount) break;
    lcd.setCursor(0, row);
    lcd.print(menuItems[idx]);

    // print star on the right side of the selected item
    if (idx == selectedIndex) {
      lcd.setCursor(19, row);
      lcd.print(F("*"));
    }
  }
  menuDrawn = true;
}

void move_up() {
  if (selectedIndex > 0) {
    selectedIndex--;
    if (selectedIndex < scrollOffset) scrollOffset--;
    menuDrawn = false;
  }
}

void move_down() {
  if (selectedIndex < menuItemCount - 1) {
    selectedIndex++;
    if (selectedIndex >= scrollOffset + 4) scrollOffset++;
    menuDrawn = false;
  }
}

void return_to_menu() {
  lcd.clear();
  menuDrawn = false;
  appState = STATE_MENU;
}

void select_item() {
  Serial.print(F("Selected: "));
  Serial.println(menuItems[selectedIndex]);

  switch (selectedIndex) {
    case 0: // Feed Now
      motorTriggered = false;
      appState = STATE_FEEDING;
      lcd.clear();
      break;
    case 1: // Set Schedule
      editHour   = scheduledHour;
      editMinute = scheduledMinute;
      editDays   = scheduleDays;
      dayCursor  = 0;
      schedStep  = SS_HOUR;
      schedDrawn = false;
      appState   = STATE_SCHEDULE;
      lcd.clear();
      break;
    case 2: // Set Date and Time
      { DateTime now = rtc.now();
        csValues[0] = now.year();
        csValues[1] = now.month();
        csValues[2] = now.day();
        csValues[3] = now.hour();
        csValues[4] = now.minute();
        csValues[5] = now.second();
        csField       = CS_YEAR;
        clockSetDrawn = false;
        appState      = STATE_CLOCK;
        lcd.clear();
      }
      break;
    case 3: // check date and time 
      appState = STATE_TIME;
      lcd.clear();
      break;
  }
}

// function to update LCD time, startRow controls which rows to write to
void updateLCDTime(int startRow = 0) {
  DateTime t = rtc.now();

  // date line
  lcd.setCursor(0, startRow);
  if (t.day() < 10) lcd.print(F("0"));
  lcd.print(t.day());
  lcd.print(F("-"));
  lcd.print(monthInWords[t.month()]);
  lcd.print(F("-"));
  lcd.print(t.year());
  lcd.print(F("  "));
  lcd.print(dayInWords[t.dayOfTheWeek()]);

  // time line
  lcd.setCursor(0, startRow + 1);
  int hh = t.twelveHour();
  if (hh < 10) lcd.print(F("0"));
  lcd.print(hh);
  lcd.print(F(":"));
  if (t.minute() < 10) lcd.print(F("0"));
  lcd.print(t.minute());
  lcd.print(F(":"));
  if (t.second() < 10) lcd.print(F("0"));
  lcd.print(t.second());
  lcd.print(t.isPM() ? F(" PM") : F(" AM"));
}

// function to draw the clock set screen
void drawClockSet() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Set Date & Time"));
  lcd.setCursor(0, 2); lcd.print(F("UP/DN=chg  OK=next"));
  lcd.setCursor(0, 3); lcd.print(F("#=cancel"));

  if (csField == CS_CONFIRM) {
    lcd.setCursor(0, 1); lcd.print(F("OK=save  #=cancel"));
    return;
  }

  lcd.setCursor(0, 1);
  lcd.print(csLabels[csField]);
  lcd.print(F(": "));
  lcd.print(csValues[csField]);
  clockSetDrawn = true;
}

// function to handle IR input during clock set
void handleClockSet(Key17 key) {
  if (!clockSetDrawn) drawClockSet();
  if (key == Key17::NONE) return;

  if (csField < CS_CONFIRM) {
    int f = (int)csField;
    if (key == Key17::KEY_UP) {
      csValues[f]++;
      if (csValues[f] > csMax[f]) csValues[f] = csMin[f];
      clockSetDrawn = false;
    } else if (key == Key17::KEY_DOWN) {
      csValues[f]--;
      if (csValues[f] < csMin[f]) csValues[f] = csMax[f];
      clockSetDrawn = false;
    } else if (key == Key17::KEY_OK) {
      csField = (ClockSetField)(f + 1);
      clockSetDrawn = false;
    }
  } else {
    // confirm screen
    if (key == Key17::KEY_OK) {
      rtc.adjust(DateTime(csValues[0], csValues[1], csValues[2],
                          csValues[3], csValues[4], csValues[5]));
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(F("Time saved!"));
      delay(1500);
      return_to_menu();
    }
  }
}

// function to draw the schedule set screen
void drawSchedule() {
  lcd.clear();
  switch (schedStep) {

    case SS_HOUR:
      lcd.setCursor(0, 0); lcd.print(F("Set Feed Hour"));
      lcd.setCursor(0, 1);
      if (editHour < 10) lcd.print(F("0"));
      lcd.print(editHour); lcd.print(F(":00"));
      lcd.setCursor(0, 2); lcd.print(F("UP/DN=chg  OK=next"));
      lcd.setCursor(0, 3); lcd.print(F("#=cancel"));
      break;
      
    case SS_MIN:
      lcd.setCursor(0, 0); lcd.print(F("Set Feed Minute"));
      lcd.setCursor(0, 1);
      if (editHour   < 10) lcd.print(F("0")); lcd.print(editHour);
      lcd.print(F(":"));
      if (editMinute < 10) lcd.print(F("0")); lcd.print(editMinute);
      lcd.setCursor(0, 2); lcd.print(F("UP/DN=chg  OK=next"));
      lcd.setCursor(0, 3); lcd.print(F("#=cancel"));
      break;

    case SS_DAYS:
      lcd.setCursor(0, 0); lcd.print(F("select days (*=on)"));
      // row 1: SUN MON TUE WED
      lcd.setCursor(0, 1);
      for (int d = 0; d < 4; d++) {
        lcd.print(dayInWords[d]);
        lcd.print((editDays & (1 << d)) ? F("*") : F(" "));
      }
      // row 2: THU FRI SAT
      lcd.setCursor(0, 2);
      for (int d = 4; d < 7; d++) {
        lcd.print(dayInWords[d]);
        lcd.print((editDays & (1 << d)) ? F("*") : F(" "));
      }
      // row 3: current cursor position
      lcd.setCursor(0, 3);
      lcd.print(F("Cur:"));
      lcd.print(dayInWords[dayCursor]);
      lcd.print(F(" OK=tog *=done"));
      break;

    case SS_SAVE:
      lcd.setCursor(0, 0); lcd.print(F("Save schedule?"));
      lcd.setCursor(0, 1);
      if (editHour   < 10) lcd.print(F("0")); lcd.print(editHour);
      lcd.print(F(":"));
      if (editMinute < 10) lcd.print(F("0")); lcd.print(editMinute);
      lcd.setCursor(0, 2); lcd.print(F("OK=yes  #=cancel"));
      break;
  }
  schedDrawn = true;
}

// function to handle IR input during schedule set
void handleSchedule(Key17 key) {
  if (!schedDrawn) drawSchedule();
  if (key == Key17::NONE) return;

  switch (schedStep) {

    case SS_HOUR:
      if (key == Key17::KEY_UP)   { editHour = (editHour + 1)  % 24; schedDrawn = false; }
      if (key == Key17::KEY_DOWN) { editHour = (editHour + 23) % 24; schedDrawn = false; }
      if (key == Key17::KEY_OK)   { schedStep = SS_MIN; schedDrawn = false; }
      break;

    case SS_MIN:
      if (key == Key17::KEY_UP)   { editMinute = (editMinute + 1)  % 60; schedDrawn = false; }
      if (key == Key17::KEY_DOWN) { editMinute = (editMinute + 59) % 60; schedDrawn = false; }
      if (key == Key17::KEY_OK)   { schedStep = SS_DAYS; schedDrawn = false; }
      break;

    case SS_DAYS:
      if (key == Key17::KEY_UP)   { dayCursor = (dayCursor + 1) % 7;  schedDrawn = false; }
      if (key == Key17::KEY_DOWN) { dayCursor = (dayCursor + 6) % 7;  schedDrawn = false; }
      if (key == Key17::KEY_OK)   { editDays ^= (1 << dayCursor);      schedDrawn = false; }
      if (key == Key17::KEY_STAR) { schedStep = SS_SAVE;               schedDrawn = false; }
      break;

    case SS_SAVE:
      if (key == Key17::KEY_OK) {
        scheduledHour   = editHour;
        scheduledMinute = editMinute;
        scheduleDays    = editDays;
        scheduleEnabled = (editDays != 0); // only enable if at least one day selected
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Schedule saved!"));
        delay(1500);
        return_to_menu();
      }
      break;
  }
}

void loop() {
  Key17 key = irController.getKey();

  // star toggles display, but only works from main menu
  if (key == Key17::KEY_STAR && appState == STATE_MENU) {
    toggle_display();
    return;
  }

  // sharp returns to main menu from any state
  if (key == Key17::KEY_SHARP) {
    return_to_menu();
    return;
  }

  // if display is off, skip all state logic
  if (!displayOn) return;

  /* Main Menu State */
  if (appState == STATE_MENU) {
    if (!menuDrawn) draw_menu();

    if (key != Key17::NONE) {
      switch (key) {
        case Key17::KEY_UP:   move_up();    break;
        case Key17::KEY_DOWN: move_down();  break;
        case Key17::KEY_OK:   select_item(); break;
        default: break;
      }
    }
  }

  /* Feeding State */
  else if (appState == STATE_FEEDING) {
    if (!motorTriggered) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Feeding..."));
      stepper.setCurrentPosition(0);
      stepper.moveTo(400); // one full rotation
      motorTriggered = true;
    }

    stepper.run(); // must be called every loop to drive the motor

    if (motorTriggered && stepper.distanceToGo() == 0) {
      stepper.setCurrentPosition(0); // reset position
      motorTriggered = false;
      lcd.setCursor(0, 1);
      lcd.print(F("Done!"));
      delay(1500);
      return_to_menu();
    }
  }

  /* Schedule State */
  else if (appState == STATE_SCHEDULE) {
    handleSchedule(key);
  }

  /* Clock State */
  else if (appState == STATE_CLOCK) {
    handleClockSet(key);
  }

  else if (appState == STATE_TIME){
    updateLCDTime(0); // Print time to lcd 
    lcd.setCursor(0,3); 
    lcd.print(F("#=back              ")); 
  }

  // check scheduled feeding time against RTC regardless of state
  if (scheduleEnabled && appState == STATE_MENU) {
    DateTime now = rtc.now();
    bool dayMatch = (scheduleDays & (1 << now.dayOfTheWeek())) != 0;

    if (dayMatch &&
        now.hour()   == scheduledHour &&
        now.minute() == scheduledMinute &&
        now.second() == 0 &&
        !scheduleFedThisMinute) {

      scheduleFedThisMinute = true;
      motorTriggered = false;
      appState = STATE_FEEDING;

    } else if (now.second() != 0) {
      scheduleFedThisMinute = false; // reset flag after trigger second passes
    }
  }
}