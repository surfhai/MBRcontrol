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
#include <avr/wdt.h>

//#define DEBUG
#define SERIALDEBUG(a) Serial.print(#a); Serial.print(": "); Serial.println(a);
#define SERIALDEBUG_ Serial.print("\n");

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
  FAILSAVE_MS,
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

struct Failsafe
{
  bool status_filtration;
  bool status_gas_jet;
  bool status_pressure_relief;
  bool status_waiting;
  uint32_t filtration_interval;
  uint32_t gas_jet_interval;
  uint32_t pressure_relief_interval;
  uint32_t waiting_interval;
  uint32_t counter;
  bool error;
} failsafe;

struct EEPROMAddresses
{
  // settings
  int s_filtration;
  int s_gas_jet;
  int s_pressure_relief;
  int s_waiting;
  // failsafe
  int fs_status_filtration;
  int fs_status_gas_jet;
  int fs_status_pressure_relief;
  int fs_status_waiting;
  int fs_interval_filtration;
  int fs_interval_gas_jet;
  int fs_interval_pressure_relief;
  int fs_interval_waiting;
  int fs_counter;
} addr;

// Grove Encoder
void timerIsr() {
  encoder->service();
}

void CalcEEPROMAdresses()
{
  addr.s_filtration = 0;
  addr.s_gas_jet = sizeof(state_list[StateIndex::FILTRATION].interval);
  addr.s_pressure_relief = addr.s_gas_jet + sizeof(state_list[StateIndex::GAS_JET].interval);
  addr.s_waiting = addr.s_pressure_relief + sizeof(state_list[StateIndex::PRESSURE_RELIEF].interval);
  addr.fs_status_filtration = addr.s_waiting + sizeof(state_list[StateIndex::WAITING].interval);
  addr.fs_status_gas_jet = addr.fs_status_filtration + sizeof(failsafe.status_filtration);
  addr.fs_status_pressure_relief = addr.fs_status_gas_jet + sizeof(failsafe.status_gas_jet);
  addr.fs_status_waiting = addr.fs_status_pressure_relief + sizeof(failsafe.status_pressure_relief);
  addr.fs_interval_filtration = addr.fs_status_waiting + sizeof(failsafe.status_waiting);
  addr.fs_interval_gas_jet = addr.fs_interval_filtration + sizeof(failsafe.filtration_interval);
  addr.fs_interval_pressure_relief = addr.fs_interval_gas_jet + sizeof(failsafe.gas_jet_interval);
  addr.fs_interval_waiting = addr.fs_interval_pressure_relief + sizeof(failsafe.pressure_relief_interval);
  addr.fs_counter = addr.fs_interval_waiting + sizeof(failsafe.waiting_interval);
}

void SettingsSave()
{
  /*
  Save the time settings in the EEPROM
  */
  EEPROM.put(addr.s_filtration, state_list[StateIndex::FILTRATION].interval);
  EEPROM.put(addr.s_gas_jet, state_list[StateIndex::GAS_JET].interval);
  EEPROM.put(addr.s_pressure_relief, state_list[StateIndex::PRESSURE_RELIEF].interval);
  EEPROM.put(addr.s_waiting, state_list[StateIndex::WAITING].interval);
  lcd.clear();
  lcd.print("Settings Saved");
  delay(2000);
}

void SettingsLoad()
{
  /*
  Load the time settings in the EEPROM
  */
  if (failsafe.error)
  {
    state_list[StateIndex::FILTRATION].interval = failsafe.filtration_interval;
    state_list[StateIndex::GAS_JET].interval = failsafe.gas_jet_interval;
    state_list[StateIndex::PRESSURE_RELIEF].interval = failsafe.pressure_relief_interval;;
    state_list[StateIndex::WAITING].interval = failsafe.waiting_interval;
  }
  else
  {
    EEPROM.get(addr.s_filtration, state_list[StateIndex::FILTRATION].interval);
    EEPROM.get(addr.s_gas_jet, state_list[StateIndex::GAS_JET].interval);
    EEPROM.get(addr.s_pressure_relief, state_list[StateIndex::PRESSURE_RELIEF].interval);
    EEPROM.get(addr.s_waiting, state_list[StateIndex::WAITING].interval);
  }

  lcd.clear();
  lcd.print("Settings Loaded");
  delay(2000);
}

bool CheckFailsafe()
{
  /*
  Check if the microcontroler crashed and continue the execution
  */
  failsafe.error = false;
  EEPROM.get(addr.fs_status_filtration, failsafe.status_filtration);
  EEPROM.get(addr.fs_status_gas_jet, failsafe.status_gas_jet);
  EEPROM.get(addr.fs_status_pressure_relief, failsafe.status_pressure_relief);
  EEPROM.get(addr.fs_status_waiting, failsafe.status_waiting);
  EEPROM.get(addr.fs_interval_filtration, failsafe.filtration_interval);
  EEPROM.get(addr.fs_interval_gas_jet, failsafe.gas_jet_interval);
  EEPROM.get(addr.fs_interval_pressure_relief, failsafe.pressure_relief_interval);
  EEPROM.get(addr.fs_interval_waiting, failsafe.waiting_interval);
  EEPROM.get(addr.fs_counter, failsafe.counter);
  
  if (failsafe.status_filtration)
  {
    state_index = StateIndex::FILTRATION;
    failsafe.error = true;
    EEPROM.put(addr.fs_status_filtration, false);
  }
  if (failsafe.status_gas_jet)
  {
    state_index = StateIndex::GAS_JET;
    failsafe.error = true;
    EEPROM.put(addr.fs_status_gas_jet, false);
  }
  if (failsafe.status_pressure_relief)
  {
    state_index = StateIndex::PRESSURE_RELIEF;
    failsafe.error = true;
    EEPROM.put(addr.fs_status_pressure_relief, false);
  }
  if (failsafe.status_waiting)
  {
    state_index = StateIndex::WAITING;
    failsafe.error = true;
    EEPROM.put(addr.fs_status_waiting, false);
  }

  if (failsafe.error)
  {
    state_running = true;
    EEPROM.put(addr.fs_counter, ++failsafe.counter);
    return false;
  }
  else
  {
    return true;
  }
  
}

void SetEEPROMStatus(int address)
{
  bool status = false;
  bool set_status = false;

  set_status = (addr.fs_status_filtration == address);
  EEPROM.get(addr.fs_status_filtration, status);
  if (status != set_status)
    EEPROM.put(addr.fs_status_filtration, set_status);

  set_status = (addr.fs_status_gas_jet == address);
  EEPROM.get(addr.fs_status_gas_jet, status);
  if (status !=  set_status)
    EEPROM.put(addr.fs_status_gas_jet, set_status);

  set_status = (addr.fs_status_pressure_relief == address);
  EEPROM.get(addr.fs_status_pressure_relief, status);
  if (status != set_status)
    EEPROM.put(addr.fs_status_pressure_relief, set_status);

  set_status = (addr.fs_status_waiting == address);
  EEPROM.get(addr.fs_status_waiting, status);
  if (status != set_status)
    EEPROM.put(addr.fs_status_waiting, set_status);
}

void SaveIntervalsToEEPROM()
{
  uint32_t fs_interval = 0;
  EEPROM.get(addr.fs_interval_filtration, fs_interval);
  if (fs_interval != state_list[StateIndex::FILTRATION].interval)
    EEPROM.put(addr.fs_interval_filtration, state_list[StateIndex::FILTRATION].interval);

  EEPROM.get(addr.fs_interval_gas_jet, fs_interval);
  if (fs_interval != state_list[StateIndex::GAS_JET].interval)
    EEPROM.put(addr.fs_interval_gas_jet, state_list[StateIndex::GAS_JET].interval);
    
  EEPROM.get(addr.fs_interval_pressure_relief, fs_interval);
  if (fs_interval != state_list[StateIndex::PRESSURE_RELIEF].interval)
    EEPROM.put(addr.fs_interval_pressure_relief, state_list[StateIndex::PRESSURE_RELIEF].interval);

  EEPROM.get(addr.fs_interval_waiting, fs_interval);
  if (fs_interval != state_list[StateIndex::WAITING].interval)
    EEPROM.put(addr.fs_interval_waiting, state_list[StateIndex::WAITING].interval);
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
  // Reset EEPROM to status 0
  SetEEPROMStatus(0);
  relay.channelCtrl(0);
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
    case MenuSettings::FAILSAVE_MS:
      lcd.clear();
      lcd.print(">Crashes");
      lcd.setCursor(0, 1);
      lcd.print(failsafe.counter);
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

        SetEEPROMStatus(0);

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
      if (action == Action::SELECT) 
      {
        menu_settings = -1;
        SaveIntervalsToEEPROM();
      }
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
      if (action == Action::RIGHT) menu_settings++;
      break;
    case MenuSettings::FAILSAVE_MS:
      if (action == Action::SELECT)
      {
        // Nothing will happen
      };
      if (action == Action::LEFT) menu_settings--;
      if (action == Action::RIGHT) menu_settings = MenuSettings::RETURN_MS;
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

      int state_list_menu_index;

      switch (menu_settings)
      {
        case MenuSettings::FILTRATION_MS:
          state_list_menu_index = StateIndex::FILTRATION;
          break;

        case MenuSettings::GAS_JET_MS:
          state_list_menu_index = StateIndex::GAS_JET;
          break;

        case MenuSettings::PRESSURE_RELIEF_MS:
          state_list_menu_index = StateIndex::PRESSURE_RELIEF;
          break;

        case MenuSettings::WAITING_MS:
          state_list_menu_index = StateIndex::WAITING;
          break;
      }

      if (menu_setting_edit && menu_setting_pos == 0 && action == Action::LEFT)
      {
        // decrease first time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[state_list_menu_index].interval >= 1000UL * 60UL))
          state_list[state_list_menu_index].interval -= 1000UL * 60UL;
      }
      else if (menu_setting_edit && menu_setting_pos == 0 && action == Action::RIGHT)
      {
        // increase first time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[state_list_menu_index].interval + 1000UL * 60UL <= ULONG_MAX))
          state_list[state_list_menu_index].interval += 1000UL * 60UL;

      }
      else if (menu_setting_edit && menu_setting_pos == 1 && action == Action::LEFT)
      {
        // decrease second time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[state_list_menu_index].interval >= 1000UL))
          state_list[state_list_menu_index].interval -= 1000UL;
      }
      else if (menu_setting_edit && menu_setting_pos == 1 && action == Action::RIGHT)
      {
        // increase second time value
        if ((menu_settings == MenuSettings::FILTRATION_MS
        || menu_settings == MenuSettings::GAS_JET_MS
        || menu_settings == MenuSettings::PRESSURE_RELIEF_MS
        || menu_settings == MenuSettings::WAITING_MS)
        && (state_list[state_list_menu_index].interval + 1000UL <= ULONG_MAX))
          state_list[state_list_menu_index].interval += 1000UL;
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
  SERIALDEBUG_
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
  state_list[StateIndex::CLOSE_ALL1].interval = 2UL * 1000UL;
  state_list[StateIndex::CLOSE_ALL1].relay_setting = 0;

  strcpy(state_list[StateIndex::GAS_JET].name, "Gas-Jet");
  state_list[StateIndex::GAS_JET].relay_setting = CHANNLE3_BIT;

  strcpy(state_list[StateIndex::CLOSE_ALL2].name, "Close All");
  state_list[StateIndex::CLOSE_ALL2].interval = 2UL * 1000UL;
  state_list[StateIndex::CLOSE_ALL2].relay_setting = 0;

  strcpy(state_list[StateIndex::PRESSURE_RELIEF].name, "Pressure Relief");
  state_list[StateIndex::PRESSURE_RELIEF].relay_setting = CHANNLE2_BIT;

  strcpy(state_list[StateIndex::WAITING].name, "Waiting");
  state_list[StateIndex::WAITING].relay_setting = 0;

  CalcEEPROMAdresses();
  CheckFailsafe();
  SettingsLoad();
  updateMenu();

  // Watchdog Timer 8 seconds
  wdt_enable(WDTO_8S);
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

      // Failsafe Start 
      switch (state_index)
      {
        case StateIndex::FILTRATION:
          SetEEPROMStatus(addr.fs_status_filtration);
          EEPROM.put(addr.fs_status_filtration, true);
          break;
        case StateIndex::CLOSE_ALL1:
          SetEEPROMStatus(addr.fs_status_gas_jet);
          EEPROM.put(addr.fs_status_gas_jet, true);
          break;
        case StateIndex::GAS_JET:
          SetEEPROMStatus(addr.fs_status_gas_jet);
          EEPROM.put(addr.fs_status_gas_jet, true);
          break;
        case StateIndex::CLOSE_ALL2:
          SetEEPROMStatus(addr.fs_status_pressure_relief);
          EEPROM.put(addr.fs_status_pressure_relief, true);
          break;
        case StateIndex::PRESSURE_RELIEF:
          SetEEPROMStatus(addr.fs_status_pressure_relief);
          EEPROM.put(addr.fs_status_pressure_relief, true);
          break;
        case StateIndex::WAITING:
          SetEEPROMStatus(addr.fs_status_waiting);
          EEPROM.put(addr.fs_status_waiting, true);
          break;
      }
      // Failsafe End

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

  // Watchdog reset
  wdt_reset();
}
