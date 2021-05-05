/*

Switch magnetic valves with relays on/off on a specific time schedule

1. Filtration
2. Gas-Jet
3. Pressure Relief
4. Waiting

Used Modules:
Grove LCD 16x2 RGB Backlight V4.0
https://wiki.seeedstudio.com/Grove-LCD_RGB_Backlight/

Grove Relais 4-Channel 5V
https://wiki.seeedstudio.com/Grove-4-Channel_SPDT_Relay/

Grove Encoder
https://wiki.seeedstudio.com/Grove-Encoder/

Grove LED Button
https://wiki.seeedstudio.com/Grove-LED_Button/

Created:
2020-12-04 Thorsten Gensler (thorsten.gensler at gmail com)

*/

#include <Arduino.h>
#include <Wire.h>
#include <ClickEncoder.h>
#include <TimerOne.h>
#include "rgb_lcd.h"
#include <multi_channel_relay.h>
#include <limits.h>
#include <EEPROM.h>

//#define DEBUG
#define SERIALDEBUG(a) Serial.print(#a); Serial.print(": "); Serial.println(a);

// Grove Encoder
#define ENCODER_PIN1 A0
#define ENCODER_PIN2 A1
#define WITHOUT_BUTTON
ClickEncoder *encoder;
int16_t encoder_last, encoder_value;

// Grove Button
const uint8_t button_pin = 5;
const uint8_t button_led_pin = 4;
const bool breath_mode = true;
uint8_t button_state;
uint8_t button_last_state = HIGH;
uint8_t button_led_state = LOW;
uint16_t button_led_fade_value = 0; // could maybe deleted
uint8_t button_led_fade_step = 50; // could maybe deleted
uint8_t button_led_fade_interval = 20; // could maybe deleted
uint32_t last_debounce_time = 0;
uint32_t debounce_delay = 50;
uint32_t last_led_fade_time = 0; // rename to last_led_ping

Multi_Channel_Relay relay;
rgb_lcd lcd;

uint8_t menu_main = 1;
int8_t menu_settings = -1;
bool state_running = false;
bool update_menu_again = false;
uint8_t menu_setting_pos = 0;
bool menu_setting_edit = false;

enum MenuMain
{
  BEGIN_MM, // could maybe deleted
  START_STOP_MM,
  SETTINGS_MM,
  END_MM // could maybe deleted
};

enum MenuSettings
{
  BEGIN_MS, // could maybe delted
  RETURN_MS,
  FILTRATION_MS,
  GAS_JET_MS,
  PRESSURE_RELIEF_MS,
  WAITING_MS,
  EEPROM_SAVE_MS,
  EEPROM_LOAD_MS,
  RESET_MS,
  END_MS, // could maybe deleted
  COUNTER_MS
};

struct State
{
  char name[17];
  uint32_t interval;
  uint8_t relay_setting;
} state_list[6];

enum StateIndex
{
  FILTRATION,
  CLOSE_ALL1,
  GAS_JET,
  CLOSE_ALL2,
  PRESSURE_RELIEF,
  WAITING
};

enum Action
{
  LEFT,
  RIGHT,
  SELECT
};

bool execute = false;
uint8_t state_index = 0;
uint32_t time_start = 0;
uint32_t interval = 0;

enum TimeSetting
{
  MINUTE,
  HOUR
};

char buf[17] = {'\0'};
char hour[3] = {'\0'};
char min[3] = {'\0'};
char sec[3] = {'\0'}; // increase size to 10
char remaining[10] = {'\0'}; // delete it and replace with sec

// Grove Encoder
void timerIsr() {
  encoder->service();
}

void Reset()
{
  /*
  Reset all settings to the initial conditions except the timings
  */
  state_running = false;
  interval = 0;
  state_index = 0;
  menu_main = MenuMain::START_STOP_MM;
  menu_settings = -1;
  execute = false;
  update_menu_again = false;
  menu_setting_pos = 0;
  menu_setting_edit = false;
  relay.channelCtrl(0);
}

void SettingsSave()
{
  /*
  Save the time settings in the EEPROM
  */
  int ee_address = 0;
  EEPROM.put(ee_address, state_list[StateIndex::FILTRATION].interval);
  ee_address += sizeof(state_list[StateIndex::FILTRATION].interval);
  EEPROM.put(ee_address, state_list[StateIndex::GAS_JET].interval);
  ee_address += sizeof(state_list[StateIndex::GAS_JET].interval);
  EEPROM.put(ee_address, state_list[StateIndex::PRESSURE_RELIEF].interval);
  ee_address += sizeof(state_list[StateIndex::PRESSURE_RELIEF].interval);
  EEPROM.put(ee_address, state_list[StateIndex::WAITING].interval);
  lcd.clear();
  lcd.print("Settings Saved");
  delay(2000);
}

void SettingsLoad()
{
  /*
  Load the time settings in the EEPROM
  */
  int ee_address = 0;
  EEPROM.get(ee_address, state_list[StateIndex::FILTRATION].interval);
  ee_address += sizeof(state_list[StateIndex::FILTRATION].interval);
  EEPROM.get(ee_address, state_list[StateIndex::GAS_JET].interval);
  ee_address += sizeof(state_list[StateIndex::GAS_JET].interval);
  EEPROM.get(ee_address, state_list[StateIndex::PRESSURE_RELIEF].interval);
  ee_address += sizeof(state_list[StateIndex::PRESSURE_RELIEF].interval);
  EEPROM.get(ee_address, state_list[StateIndex::WAITING].interval);
  lcd.clear();
  lcd.print("Settings Loaded");
  delay(2000);
}

void menuSetting(const char name[], uint32_t time, TimeSetting time_setting)
{
  /*
  Display time settings for given state on the LCD

  name:         Name of the setting which is displayed on the LCD
  time:         Time in milli seconds
  time_setting: Specifies what's the first displayed value
                TimeSetting::HOUR
                TimeSetting::MINUTE
  */
  hour[0] = '\0';
  min[0] = '\0';
  sec[0] = '\0';
  buf[0] = '\0';

  lcd.clear();
  (menu_setting_edit) ? lcd.print(" ") : lcd.print(">");
  lcd.print(name);
  lcd.setCursor(0, 1);

  if (time_setting == TimeSetting::HOUR)
  {
    sprintf(hour, "%3.2d", (int) (time / 1000UL / 60UL / 60UL));
    sprintf(min, "%.2d", (int) (time / 1000UL / 60UL % 60UL));
  }
  else if (time_setting == TimeSetting::MINUTE)
  {
    sprintf(min, "%3.2d", (int) (time / 1000UL / 60UL));
    sprintf(sec, "%.2d", (int) (time / 1000UL % 60UL));
  }

  (menu_setting_edit && menu_setting_pos == 0)
    ? strcpy(buf, ">")
    : strcpy(buf, " ");
  strcat(buf, (time_setting == TimeSetting::HOUR) ? hour : min);
  strcat(buf, (time_setting == TimeSetting::HOUR) ? "h " : "min ");
  (menu_setting_edit && menu_setting_pos == 1)
    ? strcat(buf, ">")
    : strcat(buf, " ");
  strcat(buf, (time_setting == TimeSetting::HOUR) ? min : sec);
  strcat(buf, (time_setting == TimeSetting::HOUR) ? "min" : "sec");
  lcd.print(buf);
}

void updateMenu() {
  /*
  Display the whole menu on the LCD
  */
  if (menu_settings == -1)
  {
    switch (menu_main)
    {
    /*

    case MenuMain::BEGIN_MM
    could be deleted because it should never reach this state
    it's constrained in executeAction

    */
    case MenuMain::BEGIN_MM:
      menu_main = MenuMain::START_STOP_MM;
      update_menu_again = true;
      break;
    case MenuMain::START_STOP_MM:
      lcd.clear();
      if (state_running)
      {
        lcd.print(" ");
        lcd.print(state_list[state_index].name);
        lcd.setCursor(0, 1);
        //lcd.print(">Stop   Settings");
        lcd.print(">Stop ");
        remaining[0] = {'\0'}; // replace with sec and increase size of sec to 10
        if (interval > 0)
          sprintf(remaining,
            "%9d",
            (int)((interval - (millis() - time_start)) / 1000UL));
        else
          sprintf(remaining,
            "%9d",
            (int)((state_list[state_index].interval - (millis() - time_start)) / 1000UL));
        lcd.print(remaining);
        lcd.print("s");
      }
      else
      {
        lcd.print(" ");
        if (interval > 0)
          lcd.print(state_list[state_index].name);
        else
          lcd.print("Ready");;

        lcd.setCursor(0, 1);

        if (interval > 0)
          lcd.print(">Resume Settings");
        else
          lcd.print(">Start  Settings");
      }
      break;
    case MenuMain::SETTINGS_MM:
      lcd.clear();
      if (state_running)
      {
        lcd.print(" ");
        lcd.print(state_list[state_index].name);
        lcd.setCursor(0, 1);
        lcd.print(" Stop  >Settings");
      }
      else
      {
        lcd.print(" ");
        if (interval > 0)
          lcd.print(state_list[state_index].name);
        else
          lcd.print("Ready");;

        lcd.setCursor(0, 1);

        if (interval > 0)
          lcd.print(" Resume>Settings");
        else
          lcd.print(" Start >Settings");
      }
      break;
    /*

    case MenuMain::END_MM
    could be deleted because it should never reach this state
    it's constrained in executeAction

    */
    case MenuMain::END_MM:
      menu_main = 1;
      update_menu_again = true;
      break;
    }
  }
  else
  {
    switch (menu_settings)
    {
    /*

    MenuSettings::BEGIN_MS
    could maybe deleted

    */
    case MenuSettings::BEGIN_MS:
      menu_settings = MenuSettings::COUNTER_MS - 2;
      update_menu_again = true;
      break;
    case MenuSettings::RETURN_MS:
      lcd.clear();
      lcd.print(">Return");
      break;
    case MenuSettings::FILTRATION_MS:
      menuSetting("Filtration",
        state_list[StateIndex::FILTRATION].interval,
        TimeSetting::MINUTE);
      break;
    case MenuSettings::GAS_JET_MS:
      menuSetting("Gas-Jet",
        state_list[StateIndex::GAS_JET].interval,
        TimeSetting::MINUTE);
      break;
    case MenuSettings::PRESSURE_RELIEF_MS:
      menuSetting("Pressure Relief",
        state_list[StateIndex::PRESSURE_RELIEF].interval,
        TimeSetting::MINUTE);
      break;
    case MenuSettings::WAITING_MS:
      menuSetting("Waiting",
        state_list[StateIndex::WAITING].interval,
        TimeSetting::MINUTE);
      break;
    case MenuSettings::EEPROM_SAVE_MS:
      lcd.clear();
      lcd.print(">Save Settings");
      break;
    case MenuSettings::EEPROM_LOAD_MS:
      lcd.clear();
      lcd.print(">Load Settings");
      break;
    case MenuSettings::RESET_MS:
      lcd.clear();
      lcd.print(">Reset Cycles");
      break;
    /*

    MenuSettings::END_MS
    could maybe deleted

    */
    case MenuSettings::END_MS:
      menu_settings = 1;
      update_menu_again = true;
      break;
    }
  }
}

void executeAction(Action action)
{
  /*
  Handling of the user inputs regarding the programm state
  */
  if (menu_settings == -1)
  {
    switch (menu_main)
    {
    case MenuMain::START_STOP_MM:
      // Action: SELECT Start, SELECT Stop, LEFT/RIGHT menu_main
      if (state_running && action == Action::SELECT)
      {
        state_running = false;

        if (interval > 0)
          interval = interval - (millis() - time_start);
        else
          interval = state_list[state_index].interval - (millis() - time_start);

        // Turn off all relays
        relay.channelCtrl(0);
        #ifdef DEBUG
        Serial.println("Relays all off.");
        #endif
      }
      else if (!state_running && action == Action::SELECT)
      {
        state_running = true;
        execute = true;
      }
      if (!state_running && action == Action::RIGHT) menu_main++;
      break;
    case MenuMain::SETTINGS_MM:
      // Action: SELECT Settings, LEFT/RIGHT manu_main
      if (action == Action::SELECT)
      {
        menu_settings = 1;
        menu_main = 1;
      }
      if (action == Action::LEFT) menu_main--;
      break;
    default:
      break;
    }
  }
  else
  {
    switch (menu_settings)
    {
    case MenuSettings::RETURN_MS:
      // Action: SELECT Return to Main Menu, LEFT/RIGHT menu_settings
      if (action == Action::SELECT) menu_settings = -1;
      /*

      Action::LEFT
      set menu_settings to it's max value to go to the
      last menu entry

      */
      if (action == Action::LEFT) menu_settings--;
      if (action == Action::RIGHT) menu_settings++;
      break;
    case MenuSettings::EEPROM_SAVE_MS:
      if (action == Action::SELECT) SettingsSave(); 
      if (action == Action::LEFT) menu_settings--;
      if (action == Action::RIGHT) menu_settings++;
      break;
    case MenuSettings::EEPROM_LOAD_MS:
      if (action == Action::SELECT) SettingsLoad();
      if (action == Action::LEFT) menu_settings--;
      if (action == Action::RIGHT) menu_settings++;
      break;
    case MenuSettings::RESET_MS:
      if (action == Action::SELECT) Reset();
      if (action == Action::LEFT) menu_settings--;
      /*

      Action::RIGHT
      set menu_settings to it's min value to go to the
      first menu entry

      */
      if (action == Action::RIGHT) menu_settings++;
      break;
    default:
      // Settings Menu: every time based setting
      // Action: SELECT menu_settings_edit, LEFT/RIGHT menu_settings
      if (menu_setting_edit && menu_setting_pos == 0 && action == Action::SELECT)
      {
        menu_setting_pos = 1;
      }
      else if (menu_setting_edit && menu_setting_pos == 1 && action == Action::SELECT)
      {
        menu_setting_pos = 0;
        menu_setting_edit = false;
      }
      else if (!menu_setting_edit && action == Action::SELECT)
      {
        menu_setting_edit = true;
      }

      if (menu_setting_edit && menu_setting_pos == 0 && action == Action::LEFT)
      {
        // decrease first time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[menu_settings].interval >= 1000UL * 60UL))
          state_list[menu_settings].interval -= 1000UL * 60UL;

        /*
        switch (menu_settings)
        {
        case MenuSettings::FILTRATION_MS:
          if (state_list[StateIndex::FILTRATION].interval >= 1000UL * 60UL)
            state_list[StateIndex::FILTRATION].interval -= 1000UL * 60UL;
          break;
        case MenuSettings::GAS_JET_MS:
          if (state_list[StateIndex::GAS_JET].interval >= 1000UL * 60UL)
            state_list[StateIndex::GAS_JET].interval -= 1000UL * 60UL;
          break;
        case MenuSettings::PRESSURE_RELIEF_MS:
          if (state_list[StateIndex::PRESSURE_RELIEF].interval >= 1000UL * 60UL)
            state_list[StateIndex::PRESSURE_RELIEF].interval -= 1000UL * 60UL;
          break;
        case MenuSettings::WAITING_MS:
          if (state_list[StateIndex::WAITING].interval >= 1000UL * 60UL)
            state_list[StateIndex::WAITING].interval -= 1000UL * 60UL;
          break;
        }
        */
      }
      else if (menu_setting_edit && menu_setting_pos == 0 && action == Action::RIGHT)
      {
        // increase first time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[menu_settings].interval + 1000UL * 60UL <= ULONG_MAX))
          state_list[menu_settings].interval += 1000UL * 60UL;

        /*
        switch (menu_settings)
        {
        case MenuSettings::FILTRATION_MS:
          if (state_list[StateIndex::FILTRATION].interval + 1000UL * 60UL <= ULONG_MAX)
            state_list[StateIndex::FILTRATION].interval += 1000UL * 60UL;
          break;
        case MenuSettings::GAS_JET_MS:
          if (state_list[StateIndex::GAS_JET].interval + 1000UL * 60UL <= ULONG_MAX)
            state_list[StateIndex::GAS_JET].interval += 1000UL * 60UL;
          break;
        case MenuSettings::PRESSURE_RELIEF_MS:
          if (state_list[StateIndex::PRESSURE_RELIEF].interval + 1000UL * 60UL <= ULONG_MAX)
            state_list[StateIndex::PRESSURE_RELIEF].interval += 1000UL * 60UL;
          break;
        case MenuSettings::WAITING_MS:
          if (state_list[StateIndex::WAITING].interval + 1000UL * 60UL <= ULONG_MAX)
            state_list[StateIndex::WAITING].interval += 1000UL * 60UL;
          break;
        }
        */
      }
      else if (menu_setting_edit && menu_setting_pos == 1 && action == Action::LEFT)
      {
        // decrease second time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[menu_settings].interval >= 1000UL))
          state_list[menu_settings].interval -= 1000UL;
        
        /*
        switch (menu_settings)
        {
        case MenuSettings::FILTRATION_MS:
          if (state_list[StateIndex::FILTRATION].interval >= 1000UL)
            state_list[StateIndex::FILTRATION].interval -= 1000UL;
          break;
        case MenuSettings::GAS_JET_MS:
          if (state_list[StateIndex::GAS_JET].interval >= 1000UL)
            state_list[StateIndex::GAS_JET].interval -= 1000UL;
          break;
        case MenuSettings::PRESSURE_RELIEF_MS:
          if (state_list[StateIndex::PRESSURE_RELIEF].interval >= 1000UL)
            state_list[StateIndex::PRESSURE_RELIEF].interval -= 1000UL;
          break;
        case MenuSettings::WAITING_MS:
          if (state_list[StateIndex::WAITING].interval >= 1000UL)
            state_list[StateIndex::WAITING].interval -= 1000UL;
          break;
        }
        */
      }
      else if (menu_setting_edit && menu_setting_pos == 1 && action == Action::RIGHT)
      {
        // increase second time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[menu_settings].interval + 1000UL <= ULONG_MAX))
          state_list[menu_settings].interval += 1000UL;
        
        /*
        switch (menu_settings)
        {
        case MenuSettings::FILTRATION_MS:
          if (state_list[StateIndex::FILTRATION].interval + 1000UL <= ULONG_MAX)
            state_list[StateIndex::FILTRATION].interval += 1000UL;
          break;
        case MenuSettings::GAS_JET_MS:
          if (state_list[StateIndex::GAS_JET].interval + 1000UL <= ULONG_MAX)
            state_list[StateIndex::GAS_JET].interval += 1000UL;
          break;
        case MenuSettings::PRESSURE_RELIEF_MS:
          if (state_list[StateIndex::PRESSURE_RELIEF].interval + 1000UL <= ULONG_MAX)
            state_list[StateIndex::PRESSURE_RELIEF].interval += 1000UL;
          break;
        case MenuSettings::WAITING_MS:
          if (state_list[StateIndex::WAITING].interval + 1000UL <= ULONG_MAX)
            state_list[StateIndex::WAITING].interval += 1000UL;
          break;
        }
        */
      }
      else if (!menu_setting_edit && action == Action::LEFT)
      {
        menu_settings--;
      }
      else if (!menu_setting_edit && action == Action::RIGHT)
      {
        menu_settings++;
      }
      break;
    }
  }
  #ifdef DEBUG
  SERIALDEBUG(menu_main)
  SERIALDEBUG(menu_settings)
  SERIALDEBUG(menu_setting_pos)
  SERIALDEBUG(menu_setting_edit)
  SERIALDEBUG(state_running)
  SERIALDEBUG(time_start)
  SERIALDEBUG(interval)
  SERIALDEBUG(state_index)
  SERIALDEBUG(action)
  SERIALDEBUG(execute)
  #endif
}

void setup()
{
  // Grove Button
  pinMode(button_pin, INPUT);
  pinMode(button_led_pin, OUTPUT);
  digitalWrite(button_led_pin, button_led_state);

  // Grove LCD
  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);
  lcd.print("Initialize...");
  
  // Grove Encoder
  encoder = new ClickEncoder(ENCODER_PIN1, ENCODER_PIN2, -1, 4, LOW);
  encoder->setDoubleClickEnabled(false);
  encoder->setButtonHeldEnabled(false);
  encoder->setAccelerationEnabled(true);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr); 

  #ifdef DEBUG
  Serial.begin(9600);
  while (!Serial) {}
  #endif

  // Grove Relay
  // Scan I2C device detect device address
  uint8_t relay_old_address = relay.scanI2CDevice();
  #ifdef DEBUG
  Serial.print("relay old address: ");
  Serial.println(relay_old_address);
  //if ((0x00 == relay_old_address) || (0xff == relay_old_address))
  //{
  //  Serial.println("Grove Relay old address!");
  //  while(1);
  //}

  Serial.println("Start write address");
  #endif
  relay.changeI2CAddress(relay_old_address, 0x11);
  #ifdef DEBUG
  Serial.println("End write address");

  // Read firmware  version
  Serial.print("firmware version: ");
  Serial.print("0x");
  Serial.print(relay.getFirmwareVersion(), HEX);
  Serial.println();
  #endif

  // Initial Relay State Turned off
  relay.channelCtrl(0);

  // Setup Relays
  strcpy(state_list[StateIndex::FILTRATION].name, "Filtration");
  state_list[StateIndex::FILTRATION].relay_setting = CHANNLE1_BIT;

  strcpy(state_list[StateIndex::CLOSE_ALL1].name, "Close All");
  state_list[StateIndex::CLOSE_ALL1].relay_setting = 0;

  strcpy(state_list[StateIndex::GAS_JET].name, "Gas-Jet");
  state_list[StateIndex::GAS_JET].relay_setting = CHANNLE3_BIT;

  strcpy(state_list[StateIndex::CLOSE_ALL2].name, state_list[1].name);
  state_list[StateIndex::CLOSE_ALL2].relay_setting = state_list[1].relay_setting;

  strcpy(state_list[StateIndex::PRESSURE_RELIEF].name, "Pressure Relief");
  state_list[StateIndex::PRESSURE_RELIEF].relay_setting = CHANNLE2_BIT;

  strcpy(state_list[StateIndex::WAITING].name, "Waiting");
  state_list[StateIndex::WAITING].relay_setting = 0;

  // Initial Time Settings -> Stored in EEPROM
  //state_list[StateIndex::FILTRATION].interval = 6UL * 60UL * 1000UL;
  //state_list[StateIndex::CLOSE_ALL1].interval = 2UL * 1000UL;
  //state_list[StateIndex::GAS_JET].interval = 1UL * 60UL * 1000UL;
  //state_list[StateIndex::CLOSE_ALL2].interval = state_list[1].interval;
  //state_list[StateIndex::PRESSURE_RELIEF].interval = 1UL * 1000UL;
  //state_list[StateIndex::WAITING].interval = 2UL * 60UL * 1000UL;

  SettingsLoad();
  updateMenu();
}

void loop()
{
  if (update_menu_again)
  {
    updateMenu();
    update_menu_again = false;
  }

  // Grove Encoder
  encoder_value += encoder->getValue();
  if (encoder_value != encoder_last) {
    if (encoder_value > encoder_last)
    {
      executeAction(Action::RIGHT);
      updateMenu();
    }
    else if (encoder_value < encoder_last)
    {
      executeAction(Action::LEFT);
      updateMenu();
    }
    encoder_last = encoder_value;
  }

  // Grove Button
  uint8_t button_reading = digitalRead(button_pin);
  if (button_reading != button_last_state)
    last_debounce_time = millis();
  
  if ((millis() - last_debounce_time) > debounce_delay)
  {
    if (button_reading != button_state)
    {
      button_state = button_reading;
      if (button_state == LOW)
      {
        executeAction(Action::SELECT);
        updateMenu();
        if (state_running)
          button_led_state = HIGH;
        else
          button_state = LOW;
        button_led_fade_value = 0; // could maybe deleted
        last_led_fade_time = millis();
      }
    }
  }
  button_last_state = button_reading;  

  if (state_running)
  {
    // Grove Button LED
    if (breath_mode)
    {
      if (millis() - last_led_fade_time > 700)
      {
        last_led_fade_time = millis();
        digitalWrite(button_led_pin, button_led_state);
        button_led_state = !button_led_state;
      }
    }
    // Next State only one time execution
    if (execute)
    {
      execute = false;
      time_start = millis();
      relay.channelCtrl(state_list[state_index].relay_setting);
    }
    if ((interval > 0 && millis() - time_start >= interval)
    || (interval == 0 && millis() - time_start >= state_list[state_index].interval))
    {
      // start from the beginning if the max value is reached
      if (state_index < sizeof(state_list)/sizeof(state_list[0]) - 1)
        state_index++;
      else
        state_index = 0;
      
      execute = true;
      time_start = millis();
      interval = 0;
      #ifdef DEBUG
      Serial.print("state_index: ");
      Serial.println(state_index);
      Serial.print("state name: ");
      Serial.println(state_list[state_index].name);
      #endif
    }
    if (millis() % 1000 == 0)
      updateMenu();
  }
    else
    {
      digitalWrite(button_led_pin, LOW);
    }
}
