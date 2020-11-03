#include <TroykaTextLCD.h>
#include <Encoder.h>
#include <usbh_midi.h>
#include <usbhub.h>

#define JACK_RX

// the most terrible formatting style I can think of
// hotlinks
// https://github.com/g200kg/zoom-ms-utility/blob/master/midimessage.md

USBHost UsbH;
USBHub Hub(&UsbH);
USBH_MIDI Midi(&UsbH);

constexpr uint8_t zoom_device_id = 0x5f; //MS-60b

// i2c 0x3E
// backlight 7
TroykaTextLCD lcd;

// encoder
Encoder encoder(8, SCK);

// digital buttons
constexpr int left_button = 9;
constexpr int right_button = 10;

// joystick
constexpr int joy_x = A2;
constexpr int joy_y = A1;
constexpr int joy_button = A0;

// analog in from jack pcb
constexpr int pcb_x = A3;
constexpr int pcb_y = A4;
constexpr int pcb_z = A5;

constexpr int analog_resolution = 12;
// ~5% off from potentiometer
constexpr int joystick_hardcode_min = 200;
constexpr int joystick_hardcode_max = 3895;

// would be replaced with nice calibration if necessary

#ifdef JOY_IN
constexpr int ch1 = joy_x;
constexpr int ch2 = joy_y;
constexpr int ch3 = joy_button;
constexpr int ch1_min = joystick_hardcode_min;
constexpr int ch1_max = joystick_hardcode_max;
constexpr int ch2_min = joystick_hardcode_min;
constexpr int ch2_max = joystick_hardcode_max;
#endif
#ifdef JACK_ADC
constexpr int ch1 = pcb_x;
constexpr int ch2 = pcb_y;
constexpr int ch3 = pcb_z;
constexpr int ch1_min = 1515;
constexpr int ch1_max = 1915;
constexpr int ch2_min = 25;
constexpr int ch2_max = 3195;
#endif
#ifdef JACK_RX
constexpr int ch1_min = 0;
constexpr int ch1_max = 0;
constexpr int ch2_min = 4095;
constexpr int ch2_max = 4095;
#endif

constexpr unsigned long initial_delay = 10000;
constexpr unsigned long interval = 0;

void taskful_wait(const unsigned long from, const unsigned long how_long) {
  if (how_long == 0) return;
  
  while (millis() - from <= how_long) {
    UsbH.Task();
  }
}

int parameter_map(int ch_input, const int ch_min, const int ch_max, const int param_min, const int param_max) {
  // handle values outside and on edges of range without math
  if (ch_input <= ch_min) return param_min;
  if (ch_input >= ch_max) return param_max;

  // terrible dot exe
  const int param_steps = param_max - param_min;
  const int ch_step = (ch_max - ch_min) / param_steps;
  const int param_step = (ch_input - ch_min) / ch_step;

  // protect parameters
  //if (param_step < 0) return param_min;
  const int candidate = param_step + param_min;
  if (candidate > param_max) return param_max;

  return candidate;
}

int channel_read_impl(const int channel_id) {
#ifdef JOY_IN
  if (channel_id == 1) return analogRead(ch1);
  if (channel_id == 2) return analogRead(ch2);
  return -1;
#endif
#ifdef JACK_ADC
  if (channel_id == 1) return analogRead(ch1);
  if (channel_id == 2) return analogRead(ch2);
  return -1;
#endif
#ifdef JACK_RX
while (Serial5.available()) {
  const int b = Serial5.read();
  Serial.write(b);
}
  return 0; // TODO
#endif
  return -2;
}


int read_ch1() {
  return channel_read_impl(1);
}

int read_ch2() {
  return channel_read_impl(2);
}

int ch1_to_wah() {
  const int ch1_val = read_ch1();
  const int wah = parameter_map(ch1_val, ch1_min, ch1_max, 0, 49);

  lcd.setCursor(0, 0);
  lcd.print("Ch1 ");
  lcd.print(ch1_val);
  lcd.print(" ");

  lcd.setCursor(10, 0);
  lcd.print("Wah ");
  lcd.print(wah);
  lcd.print(" ");

  return wah;
}

// debug mostly
int ch2_to_percent() {
  const int ch2_val = read_ch2();
  const int percent = parameter_map(ch2_val, ch2_min, ch2_max, 0, 99);

  lcd.setCursor(0, 1);
  lcd.print("Ch2 ");
  lcd.print(ch2_val);
  lcd.print(" ");

  lcd.setCursor(10, 1);
  lcd.print("%   ");
  lcd.print(percent);
  lcd.print(" ");

  return percent;
}

void dispose_of_incoming() {
  UsbH.Task();

  uint16_t rcvd;
  uint8_t bufMidi[64];

  if (Midi.RecvData(&rcvd,  bufMidi) == 0) {
    ;
  }

  //lcd.setCursor(10, 1);
  //lcd.print("In  ");
  //lcd.print(rcvd);
  //lcd.print(" ");

  UsbH.Task();
}

size_t write_to_usb(const uint8_t* buf, const uint16_t sz) {
  UsbH.Task();

  size_t sent = Midi.SendSysEx(const_cast<uint8_t*>(buf), sz, 0);

  UsbH.Task();

  return sent;
}

void setup() {
  const unsigned long start_millis = millis();

  Serial5.begin(9600);
  while(!Serial5);

  // 16 characters, 2 lines
  lcd.begin(16, 2);
  lcd.setContrast(45);
  lcd.setBrightness(255);

  pinMode(left_button, INPUT);
  pinMode(right_button, INPUT);

  analogReadResolution(analog_resolution);
  pinMode(joy_button, INPUT);

  if (UsbH.Init()) {
    lcd.setCursor(0, 0);
    lcd.print("USB host DOWN");
    while (1); //halt
  }

  lcd.setCursor(0, 0);
  lcd.print("USB host UP");
  lcd.setCursor(0, 1);
  lcd.print("Waiting for Zoom");

  taskful_wait(start_millis, initial_delay); // zoom startup, TODO actual check and identify request

  lcd.clear();

  // write enable parameter edit enable
  constexpr uint8_t parameter_edit_enable[] { 0xf0, 0x52, 0x00, zoom_device_id, 0x50, 0xf7 };
  write_to_usb(parameter_edit_enable, 6);

  dispose_of_incoming();
}

int last_wah = -1;
int last_percent = -1;
void loop() {
  const unsigned long start_millis = millis();

  const int wah = ch1_to_wah();
  const int percent = ch2_to_percent();

  if (wah != last_wah) {
    // don'f forget
    last_wah = wah;

    // write wah
    const uint8_t wahbuf[] { 0xf0, 0x52, 0x00, zoom_device_id, 0x31,
                             0x00, // effect 1
                             0x02, // page 1 knob 1
                             uint8_t (wah), // lsb
                             0x00, // msb formally, since wah is 0 to 49
                             0xf7
                           };

    const size_t sent = write_to_usb(wahbuf, 10);

    //lcd.setCursor(0, 1);
    //lcd.print("Out ");
    //lcd.print(sent);
    //lcd.print(" ");
  }

  dispose_of_incoming();

  taskful_wait(start_millis, interval);
}
