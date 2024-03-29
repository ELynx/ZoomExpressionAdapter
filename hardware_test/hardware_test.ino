#include <TroykaTextLCD.h>

// https://github.com/gdsports/USB_Host_Library_SAMD
#include <usbh_midi.h>
#include <usbhub.h>

//#define BRIDGE_MIDI_PROG_SERIAL

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
  constexpr int scaler = 100;

  // handle values outside and on edges of range without math
  if (ch_input <= ch_min) return param_min;
  if (ch_input >= ch_max) return param_max;
  // 0   -> min
  // 127 -> max

  // division by zero prevented above
  const int reverse = (scaler * ch_max - scaler * ch_min) / (ch_input - ch_min);
  // 1   -> 12700
  // 2   -> 6350
  // 3   -> 4233
  // 63  -> 201
  // 64  -> 198
  // 126 -> 100

  const int steps = param_max - param_min;
  // for pedal operated effects, usually 49

  const int scaled = steps * scaler / reverse;

  return param_min + scaled;
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


void print_tree_digit_value(int value) {
  if (value < -99) {
    lcd.print("---");
    return;
  }

  if (value < -9) {
    lcd.print(value);
    return;
  }

  if (value < 0) {
    lcd.print(" ");
    lcd.print(value);
    return;
  }

  if (value < 10) {
    lcd.print("  ");
    lcd.print(value);
    return;
  }

  if (value < 100) {
    lcd.print(" ");
    lcd.print(value);
    return;
  }

  if (value < 1000) {
    lcd.print(value);
    return;
  }

  lcd.print("+++");
}


int ch1_to_wah() {
  const int ch1_val = read_ch1();
  const int wah = parameter_map(ch1_val, 0, 49);

  lcd.setCursor(0, 0);
  lcd.print("Ch1 ");
  print_tree_digit_value(ch1_val);

  lcd.setCursor(9, 0);
  lcd.print("Wah ");
  print_tree_digit_value(wah);

  return wah;
}


int ch2_ch3_to_screen() {
  const int ch2_val = read_ch2();
  const int ch3_val = read_ch3();

  lcd.setCursor(0, 1);
  lcd.print("Ch2 ");
  print_tree_digit_value(ch2_val);

  lcd.setCursor(9, 1);
  lcd.print("Ch3 ");
  print_tree_digit_value(ch3_val);

  return ch2_val << 8 + ch3_val;
}


size_t write_to_usb(const uint8_t* midi_buffer, const uint16_t to_be_sent) {
  UsbH.Task();

  const size_t return_code = Midi.SendSysEx(const_cast<uint8_t*>(midi_buffer), to_be_sent, 0);

  UsbH.Task();

  return return_code == 0 ? to_be_sent : 0;
}


void dispose_of_incoming() {
  UsbH.Task();

  uint16_t received;
  uint8_t midi_buffer[64];

  // returned value is return code
  if (Midi.RecvData(&received,  midi_buffer) == 0) {
#ifdef BRIDGE_MIDI_PROG_SERIAL
    if (received > 0) {
      global_ch_2 = Serial.write(midi_buffer, received);
    }
#else
    ;
#endif
  }

#ifdef BRIDGE_MIDI_PROG_SERIAL
  UsbH.Task();

  auto command_size = Serial.available();
  if (command_size > 0) {
    auto command_size_actual = Serial.readBytes(midi_buffer, command_size);
    global_ch_3 = write_to_usb(midi_buffer, command_size_actual);
  }
#endif

  UsbH.Task();
}


constexpr unsigned long initial_delay = 10000;
constexpr unsigned long interval = 0;

void setup() {
  const unsigned long start_millis = millis();

#ifdef BRIDGE_MIDI_PROG_SERIAL
  Serial.begin(57600);
  while (!Serial);
#endif

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
  }

  dispose_of_incoming();

  taskful_wait(start_millis, interval);
}
