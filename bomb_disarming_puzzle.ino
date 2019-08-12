/*
 * Project: An electronic puzzle box
 * Author: Connor Claypool
 * Date: 26-11-2018
 */

#include <Streaming.h>
#include <DS3231.h>
#include <Wire.h>
#include <TM1638.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <cmath>

//// SECTION: HARDWARE DEVICES AND CONNECTIONS

/*
 * -- Power connections --
 * WEMOS pin 3V3 : breadboard power rail (red wire)
 * WEMOS pin GND : breadboard ground rail (brown wire)
 */

/*
 * -- TM1638 expansion module hardware connections --
 * TM1638 VCC : breadboard power rail (3.3V) (red wire)
 * TM1638 GND : breadboard ground rail (brown wire)
 * TM1638 STB : WEMOS pin D5 (green wire)
 * TM1638 CLK : WEMOS pin D6 (purple wire)
 * TM1638 DIO : WEMOS pin D7 (grey wire)
 */
 
// define pins used to connect TM1638 to WEMOS
#define TM1638_STB D5
#define TM1638_CLK D6
#define TM1638_DIO D7
// create object for TM1638 expansion module
TM1638 expansion_module(TM1638_DIO, TM1638_CLK, TM1638_STB);

/*
 * -- SSD1306 OLED display hardware connections --
 * SSD1306 VCC : breadboard power rail (3.3V) (white wire)
 * SSD1306 GND : breadboard ground rail (black wire)
 * SSD1306 SCL : WEMOS pin SCL/D1 (orange wire)
 * SSD1306 SDA : WEMOS pin SDA/D2 (yellow wire)
 */

// define reset argument and screen I2C address
#define OLED_RESET -1
#define OLED_SCREEN_I2C_ADDRESS 0x3C
// create object for SSD1306 OLED screen
Adafruit_SSD1306 OLED_display(OLED_RESET);

/*
 * -- DS3231 Real Time Clock (RTC) hardware connections --
 * DS3231 VCC : breadboard power rail (3.3V) (red wire)
 * DS3231 GND : breadboard ground rail (brown wire)
 * DS3231 SCL : WEMOS pin SCL/D1 (orange wire)
 * DS3231 SDA : WEMOS pin SDA/D2 (yellow wire)
 */

// create object for DS3231 Real Time Clock
DS3231 rtc;

/*
 * -- Rotary encoder hardware connections --
 * Rotary encoder pin 1: breadboard power rail (3.3V) (white wire)
 * Rotary encoder pin 2 (central pin): WEMOS pin A0 (blue wire)
 * Rotary encoder pin 3: breadboard ground rail (black wire)
 */

// Define pin A0 as rotary encoder pin
#define ROTARY_ENCODER A0

//// SECTION: GLOBAL VARIABLES (GENERAL)

// current stage, defines which loop function is run
int8_t stage = 1;
// whether time has finished (success or fail)
bool finished = false;

// variables relating to button input
byte prev_buttons = 0;
byte buttons = 0;
uint8_t button_presses = 0;
uint8_t buttons_holdtime = 0;

// variables relating to rotary encoder
uint8_t encoder_value = 0;
uint8_t prev_encoder_value = 255;
uint8_t encoder_holdtime = 0;

// variables relating to countdown time
uint32_t starting_timestamp = 0;
uint16_t elapsed_time = 0;
uint16_t penalty = 0;

// masks used to access individual bits of byte
byte byte_masks[8] = {
  0b00000001,
  0b00000010,
  0b00000100,
  0b00001000,
  0b00010000,
  0b00100000,
  0b01000000,
  0b10000000
};

// masks used to access groups of 4 bits in 16 bit number
uint16_t hex_digit_masks_16bit[4] = {
  0b1111000000000000,
  0b0000111100000000,
  0b0000000011110000,
  0b0000000000001111
};

//// SECTION: FUNCTION DEFINITIONS (HELPER FUNCTIONS)

// updates prev_buttons and gets current button press
void get_buttons() {
  prev_buttons = buttons;
  buttons = expansion_module.getButtons();
}

// updates current and previous rotary encoder values
void get_encoder_value() {
  prev_encoder_value = encoder_value;
  encoder_value = analogRead(ROTARY_ENCODER) / 100;
}

// takes the time remaining as a 4 digit integer with the format HHMM
// and displays it on the expansion board's 7-segment display
void display_countdown(uint16_t time_hm) {

  uint8_t digits[4] = { 0 };
  uint8_t i = 0;

  // split 4-digit number into constituent digits
  if(time_hm < 10) {
    i = 3;
  } else if(time_hm < 100) {
    i = 2;
  } else if(time_hm < 1000) {
    i = 1;
  }
  for(; i < 4; i++) {
    digits[i] = (uint8_t)((int)(time_hm / pow(10, (3 - i))) % 10);
  }

  // display digits on 7-segment display, placing a point
  // between the hours and minutes
  for(i = 0; i < 4; i++) {
    bool pt = false;
    if(i == 1) {
      pt = true;
    }
    expansion_module.setDisplayDigit(digits[i], 4 + i, pt);
  }
}

// calculates the time remaining and displays this on the 7-segment display
void update_countdown() {

  // calculate elapsed time including penalty time
  elapsed_time = rtc.getTimestamp() - starting_timestamp + penalty;

  // if there is time remaining, calculate 4-digit integer representing
  // time with format HHMM and pass this to display_countdown
  if(elapsed_time <= 3599)
  {
    uint16_t time_remaining = 3599 - elapsed_time;
    uint16_t formatted_time = ((time_remaining / 60) * 100) + time_remaining % 60;
    display_countdown(formatted_time);
  } else {
    // if time has run out, change stage to -1 (fail stage)
    stage = -1;
  }
}

// count button presses and penalize time if 
// no. of button presses exceeds a certain amount
void buttons_penalty(uint8_t max_presses) {
  // if buttons have been pressed and released, increment counter
  if(buttons == 0 && prev_buttons != 0) {
    button_presses++;
  }
  // if presses exceeds specified amount, add 10s penalty time
  if(button_presses > max_presses) {
    penalty += 10;
    button_presses = 0;
    // flash LEDs above countdown to indicate penalty
    for(uint8_t i = 0; i < 3; i++) {
      expansion_module.setLEDs(0b11110000);
      delay(75);
      expansion_module.setLEDs(0b00000000);
      delay(75);
    }
  }
}

//// SECTION: STAGE 1 GLOBAL VARIABLES, HELPER FUNCTIONS AND LOOP FUNCTION

// stage 1 loop function
void stage1_loop() {

  // update countdown on display and get button presses
  update_countdown();
  get_buttons();
  // apply 10s penalty after every 16 button presses
  buttons_penalty(16);

  // if the leftmost 4 buttons are pressed
  if(buttons == 0b00001111) {
    // flash leftmost 4 LEDs to indicate stage passed
    for(uint8_t i = 0; i < 3; i++) {
      expansion_module.setLEDs(0b00001111);
      delay(75);
      expansion_module.setLEDs(0b00000000);
      delay(75);
    }
    // reset button presses and progress to stage 2
    button_presses = 0;
    stage = 2;
  }
}

//// SECTION: STAGE 2 GLOBAL VARIABLES, HELPER FUNCTIONS AND LOOP FUNCTION

// 16 bit binary numbers (i.e. 4-digit hex numbers) associated with each button
uint16_t stage2_button_codes[8] = {
  0b1110100010100010,
  0b0010110101001011,
  0b0010010010110101,
  0b1010010101001010,
  0b0010101010010101,
  0b1111100000100001,
  0b1001010010010100,
  0b1010010100101001
};

// combined button code for pressed buttons
uint16_t combined_button_code = 0;

// combine button codes of all pressed buttons using bitwise XOR
void get_combined_button_code() {

  combined_button_code = 0;
  for(uint8_t i = 0; i < 8; i ++) {
    // use mask and bitwise AND to test whether each button is pressed
    if(buttons & byte_masks[i]) {
      combined_button_code ^= stage2_button_codes[i];
    }
  }
}

// display 16 bit combined button code as 4-digit hex number
// on leftmost 4 digits of 7-segment LED display
void display_combined_button_code() {

  // calculate each digit by using mask to select relevant 4 bits
  // and shifting these to be the rightmost (least significant) 4
  for(uint8_t i = 0; i < 4; i++) {
    uint8_t digit = (combined_button_code & hex_digit_masks_16bit[i]) >> (4 * (3 - i));
    // display digit at relevant position
    expansion_module.setDisplayDigit(digit, i, false);
  }
}

// stage 2 loop function
void stage2_loop() {

  // update countdown and get button presses
  update_countdown();
  get_buttons();
  // apply 10s penalty after every 16 button presses
  buttons_penalty(16);

  // calculate combined button code for pressed buttons and
  // display these on 7-seg LEDs
  get_combined_button_code();
  display_combined_button_code();
  // if code is 0xFFFF, i.e. all bits are set
  if(combined_button_code == 0xffff) {
    // reset button presses and advance to stage 3
    button_presses = 0;
    stage = 3;
  } 
}

//// SECTION: STAGE 3 GLOBAL VARIABLES, HELPER FUNCTIONS AND LOOP FUNCTION

// sequence of patterns to be displayed on LEDs
byte LED_patterns[4] = { 
  0b00001010,
  0b00011000,
  0b10000011, 
  0b01000011
};
// counter for current LED pattern, pattern changes at every multiple of 5
uint8_t LED_pattern_counter = 0;
// index of current pattern displayed on LEDs
uint8_t current_LED_pattern = 0;
// whether each pattern has been matched
bool matched_LED_patterns[4] = { false };
// total number of matched patterns
uint8_t num_matched = 0;

// stage 3 loop function
void stage3_loop() {

  // update countdown and get button presses
  update_countdown();
  get_buttons();

  // display current LED pattern on LEDs
  expansion_module.setLEDs(LED_patterns[current_LED_pattern]);

  // if buttons match pattern and this pattern has not already been matched,
  // set this pattern to matched and increment matched counter
  if(buttons == LED_patterns[current_LED_pattern] && !matched_LED_patterns[current_LED_pattern]) {
    matched_LED_patterns[current_LED_pattern] = true;
    expansion_module.setDisplayDigit(0, current_LED_pattern, false);
    num_matched++;
  // if buttons do not match pattern, this pattern has previously been matched and at least
  // one button is pressed, set this pattern to not matched and decrement matched counter
  } else if(buttons != LED_patterns[current_LED_pattern] && buttons != 0 && matched_LED_patterns[current_LED_pattern]) {
    matched_LED_patterns[current_LED_pattern] = false;
    expansion_module.setDisplayDigit(15, current_LED_pattern, false);
    num_matched--;
  }

  // increment pattern counter, moving to next pattern if counter is multiple of 5
  LED_pattern_counter = (LED_pattern_counter + 1) % 5;
  if(LED_pattern_counter == 0) {
    current_LED_pattern = (current_LED_pattern + 1) % 4;
  }

  // once all patterns have been matched, turn LEDs off and advance to stage 4
  if(num_matched == 4) {
    expansion_module.setLEDs(0);
    stage = 4;
  }
}

//// SECTION: STAGE 4 GLOBAL VARIABLES, HELPER FUNCTIONS AND LOOP FUNCTION

// whether lines are set to 1 or 0 (on or off)
bool cmd = false;
bool fail = false;
// intermediate state of circuit, calculated from input state
// and from which output state is calculated
bool intermediate_state[4] = { false };
// time lines have been on for
uint8_t cmd_holdtime;
uint8_t fail_holdtime;

// draw input lines to circuit, drawing them double thickness
// if relevant button is pressed i.e. input is set to 1
void draw_input_lines() {
  
  OLED_display.drawFastHLine(0, 0, 39, 1);
  OLED_display.drawFastVLine(39, 0, 14, 1);
  if(buttons & byte_masks[0]) {
    OLED_display.drawFastHLine(0, 1, 39, 1);
    OLED_display.drawFastVLine(38, 0, 14, 1);
  }

  OLED_display.drawFastHLine(0, 9, 30, 1);
  OLED_display.drawFastVLine(30, 9, 5, 1);
  if(buttons & byte_masks[1]) {
    OLED_display.drawFastHLine(0, 10, 30, 1);
    OLED_display.drawFastVLine(29, 9, 5, 1);
  }

  OLED_display.drawFastHLine(0, 18, 24, 1);
  if(buttons & byte_masks[2]) {
    OLED_display.drawFastHLine(0, 19, 24, 1);
  }
  
  OLED_display.drawFastHLine(0, 27, 24, 1);
  if(buttons & byte_masks[3]) {
    OLED_display.drawFastHLine(0, 28, 24, 1);
  }
  
  OLED_display.drawFastHLine(0, 36, 24, 1);
  if(buttons & byte_masks[4]) {
    OLED_display.drawFastHLine(0, 35, 24, 1);
  }
  
  OLED_display.drawFastHLine(0, 45, 24, 1);
  if(buttons & byte_masks[5]) {
    OLED_display.drawFastHLine(0, 44, 24, 1);
  }
  
  OLED_display.drawFastHLine(0, 54, 30, 1);
  OLED_display.drawFastVLine(30, 50, 5, 1);
  if(buttons & byte_masks[6]) {
    OLED_display.drawFastHLine(0, 53, 30, 1);
    OLED_display.drawFastVLine(29, 50, 5, 1);
  }

  
  OLED_display.drawFastHLine(0, 63, 39, 1);
  OLED_display.drawFastVLine(39, 50, 14, 1);
  if(buttons & byte_masks[7]) {
    OLED_display.drawFastHLine(0, 62, 39, 1);
    OLED_display.drawFastVLine(38, 50, 14, 1);
  } 
}

// draw output lines from circuit, double thickness if set to 1
void draw_output_lines() {

  OLED_display.drawFastHLine(60, 23, 34, 1);
  OLED_display.drawFastVLine(93, 0, 23, 1);
  OLED_display.drawFastHLine(93, 0, 34, 1);
  if(fail) {
    OLED_display.drawFastHLine(60, 24, 34, 1);
    OLED_display.drawFastVLine(92, 0, 23, 1);
    OLED_display.drawFastHLine(93, 1, 34, 1);
  }

  OLED_display.drawFastHLine(60, 40, 34, 1);
  OLED_display.drawFastVLine(93, 40, 34, 1);
  OLED_display.drawFastHLine(93, 63, 34, 1);
  if(cmd) {
    OLED_display.drawFastHLine(60, 39, 34, 1);
    OLED_display.drawFastVLine(92, 40, 34, 1);
    OLED_display.drawFastHLine(93, 62, 34, 1);
  }
  
}

// draw circuit on OLED display
void draw_circuit() {

  // initialize display
  OLED_display.clearDisplay();
  OLED_display.setCursor(0,0);

  // draw input lines based on button presses
  draw_input_lines();

  // draw central IC chip
  OLED_display.drawRect(24, 14, 36, 36, 1);
  // draw text for logical operation symbols and output labels
  OLED_display.setCursor(34, 29);
  OLED_display << "^ &";
  OLED_display.setCursor(106, 4);
  OLED_display << "!!!";
  OLED_display.setCursor(106, 53);
  OLED_display << "cmd";

  // draw output lines based on calculated values
  draw_output_lines();

  // update display
  OLED_display.display();
}

// stage 4 loop function
void stage4_loop() {

  // update countdown and get buttons
  update_countdown();
  get_buttons();

  // calculate intermediate state from input state (XOR of pairs of inputs)
  for(uint8_t i = 0; i <= 6; i+=2) {
    intermediate_state[i / 2] = (bool)(buttons & byte_masks[i]) != (bool)(buttons & byte_masks[i+1]);
  }

  // calculate values of fail and cmd lines (AND of pairs of intermediate state)
  fail = intermediate_state[0] && intermediate_state[1];
  cmd = intermediate_state[2] && intermediate_state[3];

  // draw circuit on OLED display
  draw_circuit();

  // if fail line is activated, increment fail holdtime
  if(fail) {
    fail_holdtime++;
    // if fail holdtime exceeds 5, add a 1-minute penalty every
    // iteration and activate rightmost 4 LEDs to indicate this
    if(fail_holdtime > 5) {
      penalty += 60;
      expansion_module.setLEDs(0b11110000);
    }
  // if fail is not activated, set fail holdtime to 0 and turn off LEDs
  } else {
    fail_holdtime = 0;
    expansion_module.setLEDs(0b00000000);
    // if cmd line is activated, increment cmd holdtime
    if(cmd) {
      cmd_holdtime++;
      // if cmd holdtime exceeds 10, advance to stage 5
      if(cmd_holdtime > 10) {
        stage = 5;
        stage5_setup();
      }
    // if cmd is not activated, set cmd holdtime to 0
    } else {
      cmd_holdtime = 0;
    }
  }
}

//// SECTION: STAGE 5 GLOBAL VARIABLES, HELPER FUNCTIONS AND LOOP FUNCTION

// variables for server and its client (only 1 client permitted)
WiFiServer server(2199);
WiFiClient serverClient;

// encoder value that enables PIN input
uint8_t encoder_PIN_enable;
// developer PIN used to unlock commands
uint8_t developer_PIN;
// whether PIN input is enabled
bool PIN_enabled = false;
// whether console is fully unlocked i.e. PIN has been accepted
bool network_console_unlocked = false;

// stage 5 setup function, run once on advance to stage 5
void stage5_setup() {

  // indicate on OLED display that developer access point
  // has been enabled
  OLED_display.clearDisplay();
  OLED_display.setCursor(0, 0);
  OLED_display << "Developer AP:" << endl;
  OLED_display << "enabled" << endl;
  OLED_display.display();

  // start wireless access point
  WiFi.mode(WIFI_AP);
  const char *ssid = "WEMOS-D1-112387";
  const char *password = "developerAP112387";
  WiFi.softAP(ssid, password);
  // print IP address of access point to serial console
  Serial << WiFi.softAPIP() << endl;
  
  // start server
  server.begin();
  server.setNoDelay(true);
  
  // randomly generate PIN enable value and developer PIN
  // (PIN enable cannot be current value of encoder)
  encoder_value = analogRead(ROTARY_ENCODER) / 100;
  do {
    encoder_PIN_enable = random(0, 9);
  } while(encoder_PIN_enable == encoder_value);
  developer_PIN = random(1, 255);
}

// processes input to serial console
void process_serial_console() {
  // if input is 128 (i.e max amount or higher) output character dump 
  // which encludes PIN enable and PIN values
  if(Serial.available() == 128) {
    Serial << "ouwARC98*)(*9ewiURPvap9 ijp009q239MIPvc0ir904qM0" << endl <<
              "98347 59nrotaryPINenable:" << encoder_PIN_enable << "fDS:098734jcm98345c2d4" << endl <<
              "983593u48mtr32m,t 9834umt 8c9238UMM 9283u49nm983" << endl <<
              "hjelFCXNmi oiwaejfnmoi noiwaefoiNJInmoiewkjhfkjs" << endl <<
              "DeveloperAPisauy9844u shjdeveloperPIN:" << developer_PIN << "alSJD83g" << endl;
  }
  // read input to reset available data amount
  while(Serial.available()){
    char t = Serial.read();
  }
}

// processes current value of rotary encoder
void process_rotary_encoder() {

  // get current encoder value
  get_encoder_value();
  // if encoder value has not changed, increment holdtime
  if(encoder_value == prev_encoder_value) {
    encoder_holdtime++;
    // if holdtime exceeds 20
    if(encoder_holdtime > 20) {
      // if PIN input is not enabled and encoder value is that which enables
      // PIN input, activate PIN input and output message to serial console
      if(!PIN_enabled && encoder_value == encoder_PIN_enable) {
        PIN_enabled = true;
        Serial << "PIN input enabled" << endl;
      // if PIN input is enabled and ecoder value is not that which enables
      // PIN input, deactivate PIN input and output message to serial console
      } else if(PIN_enabled && encoder_value != encoder_PIN_enable) {
        PIN_enabled = false;
        Serial << "PIN input disabled" << endl;
      }
      // reset holdtime
      encoder_holdtime = 0;
    }
  // if encoder value has changed, reset holdtime and output new
  // value to serial console
  } else {
    encoder_holdtime = 0;
    Serial << encoder_value << endl;
  }
}

// processes input to network console
void process_network_console() {
  
  // if there is a client connected and it has sent some data
  if(serverClient.connected() && serverClient.available()) {
    // read data as string
    String cmd = serverClient.readString();
    // process string as command input
    if(cmd == "help\n") {
      serverClient.write("[*] help|info|stop\n");
    } else if(cmd == "info\n") {
      serverClient.write("[!] Error: not implemented\n");
    } else if(cmd == "stop\n") {
      // allow stop command only if console is unlocked i.e. PIN has been accepted
      if(!network_console_unlocked) {
        serverClient.write("[!] Permission denied. Enter PIN on device to unlock this command.\n");
      } else {
        // once stop has been executed, advance to stage 0 (success)
        serverClient.write("[*] Countdown halted.\n");
        stage = 0;
      }
    } else {
      serverClient.write("[!] Error: command not recognised\n");
    }
  }
}

// processes button presses
void process_buttons() {
  
  // get button presses
  get_buttons();
  // if buttons value has not changed and is not 0, increment holdtime
  if(buttons != 0 && buttons == prev_buttons) {
    buttons_holdtime++;
    // if holdtime exceeds 40
    if(buttons_holdtime > 40) {
      // if PIN input is not enabled and has not already been accepted,
      // send message to network console
      if(!PIN_enabled && !network_console_unlocked) {
        serverClient.write("[!] PIN input not enabled\n");
      }
      // if PIN input is enabled, buttons match developer PIN and PIN has
      // not already been accepted, unlock network console and print message
      if(PIN_enabled && buttons == developer_PIN && !network_console_unlocked) {
        network_console_unlocked = true;
        serverClient.write("[*] PIN accepted. All commands unlocked. \n");
      }
      // reset holdtime
      buttons_holdtime = 0;
    }
   // if buttons value has changed, reset holdtime
  } else if (buttons != prev_buttons) {
    buttons_holdtime = 0;
  }
}

// stage 5 loop function
void stage5_loop() {
  
  // update countdown
  update_countdown();

  // new client replaces any existing client
  if(server.hasClient()) {
    serverClient = server.available();
  }
  
  // process input to serial console
  process_serial_console();
  // process rotary encoder value
  process_rotary_encoder();
  // process input to network console
  process_network_console();
  // process buttons input
  process_buttons();
}

//// SECTION: FUNCTIONS FOR SUCCESS AND FAILURE

// loop function for when time runs out
void time_over() {
  if(!finished) {
    // display FAIL on 7-seg LED display
    expansion_module.clearDisplay();
    expansion_module.setDisplayToString("    FAIL");
    // turn off LEDs
    expansion_module.setLEDs(0);
    // clear OLED display
    OLED_display.clearDisplay();
    OLED_display.display();
    // ensure access point and server are stopped
    server.stop();
    WiFi.mode(WIFI_STA);
    finished = true;
  }
}

// loop function for successful stop of countdown
void countdown_halted() {
  if(!finished) {
    // display END on 7-seg LED display
    expansion_module.clearDisplay();
    expansion_module.setDisplayToString(" SUCCESS");
    // clear OLED display
    OLED_display.clearDisplay();
    OLED_display.display();
    // stop server and access point
    server.stop();
    WiFi.mode(WIFI_STA);
    finished = true;
  }
}

//// SECTION: MAIN SETUP AND LOOP FUNCTIONS

// setup function, runs once
void setup() {
  Wire.begin();
  // initialize serial console
  Serial.begin(115200);
  Serial << endl;
  // initialize random number generator
  randomSeed(rtc.getTimestamp());
  // initialize 7-seg LED display
  expansion_module.clearDisplay();
  expansion_module.setupDisplay(true, 2);
  // save start time
  starting_timestamp = rtc.getTimestamp();
  // initialize OLED display
  OLED_display.begin(SSD1306_SWITCHCAPVCC, OLED_SCREEN_I2C_ADDRESS);
  OLED_display.clearDisplay();
  OLED_display.setCursor(0,0);
  OLED_display.setTextSize(1);
  OLED_display.setTextColor(1);
  OLED_display.display();
  // initialize WiFi
  WiFi.mode(WIFI_STA);
}

// loop function, runs repeatedly
void loop() {
  // run loop function depending on stage
  switch(stage) {
    case -1: time_over(); break;
    case 0: countdown_halted(); break;
    case 1: stage1_loop(); break;
    case 2: stage2_loop(); break;
    case 3: stage3_loop(); break;
    case 4: stage4_loop(); break;
    case 5: stage5_loop(); break;
    default: break;
  }
  // pause for 100ms
  delay(100);
}
