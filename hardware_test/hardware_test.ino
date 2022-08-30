#include <TroykaTextLCD.h>

// https://github.com/gdsports/USB_Host_Library_SAMD
#include <usbh_midi.h>
#include <usbhub.h>

// hotlinks
// https://github.com/g200kg/zoom-ms-utility/blob/master/midimessage.md

constexpr uint8_t zoom_device_id_ms50g   = 0x58;
constexpr uint8_t zoom_device_id_ms60b   = 0x5f;
constexpr uint8_t zoom_device_id_ms70cdr = 0x61;

constexpr uint8_t zoom_device_id = zoom_device_id_ms60b;


// USB shenanigans
USBHost UsbH;
USBHub Hub(&UsbH);
USBH_MIDI Midi(&UsbH);

// i2c 0x3E
// backlight 7
TroykaTextLCD lcd;

// digital buttons
constexpr int left_button = 8;
constexpr int right_button = 10;


void taskful_wait(const unsigned long from, const unsigned long how_long) {
  if (how_long == 0) return;

  while (millis() - from <= how_long) {
    UsbH.Task();
  }
}


int parameter_map(int ch_input, const int param_min, const int param_max) {
  // from observation
  constexpr int ch_min = 0;
  constexpr int ch_max = 127;

  // handle values outside and on edges of range without math
  if (ch_input <= ch_min) return param_min;
  if (ch_input >= ch_max) return param_max;

  // to prevent division by zero add a 1
  const int reverse = (ch_max - ch_min + 1) / (ch_input - ch_min + 1);
  // 127 -> 1
  // 63  -> 2
  // 0   -> 128

  const int scaler = param_max - param_min + 1;
  // for pedal operatedeffects, usually 50

  const int value = scaler / reverse;
  // 127 -> 50
  // 63  -> 25
  // 3   -> 1
  // 2   -> 0
  // 1   -> 0
  // 0   -> 0

  return value;
}


constexpr int SYNC_BYTE = 0xB0;
bool sync = false;

constexpr int NO_LAST_CH = -1;
int last_ch = NO_LAST_CH;

int global_ch_1 = 0;
int global_ch_2 = 0;
int global_ch_3 = 0;

void poll_serial() {
  while (Serial5.available()) {
    const int in_byte = Serial5.read();

    if (in_byte == SYNC_BYTE) {
      sync = true;
      last_ch = NO_LAST_CH;
    } else if (sync && last_ch == NO_LAST_CH) {
      last_ch = in_byte;
    } else if (sync && last_ch != NO_LAST_CH) {
      switch (last_ch) {
        case 0:
          global_ch_1 = in_byte;
          break;
        case 1:
          global_ch_2 = in_byte;
          break;
        case 2:
          global_ch_3 = in_byte;
          break;
        default:
          break;
      }

      sync = false;
      last_ch = NO_LAST_CH;
    }
  }
}


int read_ch1() {
  return global_ch_1;
}

int read_ch2() {
  return global_ch_2;
}

int read_ch3() {
  return global_ch_3;
}


int ch1_to_wah() {
  const int ch1_val = read_ch1();
  const int wah = parameter_map(ch1_val, 0, 49);

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


int ch2_ch3_to_screen() {
  const int ch2_val = read_ch2();
  const int ch3_val = read_ch3();

  lcd.setCursor(0, 1);
  lcd.print("Ch2 ");
  lcd.print(ch2_val);
  lcd.print(" ");

  lcd.setCursor(10, 1);
  lcd.print("Ch3 ");
  lcd.print(ch3_val);
  lcd.print(" ");

  return ch2_val << 8 + ch3_val;
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

  const size_t sent = Midi.SendSysEx(const_cast<uint8_t*>(buf), sz, 0);

  UsbH.Task();

  return sent;
}


constexpr unsigned long initial_delay = 10000;
constexpr unsigned long interval = 0;

void setup() {
  const unsigned long start_millis = millis();

  Serial5.begin(31250); // MIDI
  while (!Serial5);

  // 16 characters, 2 lines
  lcd.begin(16, 2);
  lcd.setContrast(45);
  lcd.setBrightness(255);

  pinMode(left_button, INPUT);
  pinMode(right_button, INPUT);

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

void loop() {
  const unsigned long start_millis = millis();

  poll_serial();

  const int wah = ch1_to_wah();
  const int screen = ch2_ch3_to_screen();

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
