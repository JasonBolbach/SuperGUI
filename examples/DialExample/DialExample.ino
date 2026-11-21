// SuperDial: a round face with any number of pointers (display only): a clock, a compass with a turning card, a tachometer.
// Minimal example. Edit the pins / panel in the HARDWARE block for your board.
// Wiring: encoder A/B -> pins 2/3, ButtonA -> pin 4, ButtonB -> pin 5 (buttons to GND, internal pull-ups).
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <SuperDial.h>
#include <SuperForm.h>

// ---------------- HARDWARE (edit me) ----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI      bus;
public:
  LGFX() {
    auto b = bus.config();
#if defined(ARDUINO_ARCH_RP2040)
    b.spi_host = 0;                         // Pico: SPI0
#else
    b.spi_host = SPI2_HOST;                 // ESP32
#endif
    b.freq_write = 40000000;
    b.pin_sclk = 18; b.pin_mosi = 19; b.pin_miso = -1; b.pin_dc = 20;
    bus.config(b);
    panel.setBus(&bus);
    auto p = panel.config();
    p.pin_cs = 17; p.pin_rst = 21; p.pin_busy = -1;
    p.panel_width = 240; p.panel_height = 320;
    panel.config(p);
    setPanel(&panel);
  }
};
LGFX lcd;
SuperEncoder Enc(2, 3);
SuperButton  BtnA(4, true), BtnB(5, true);   // active low
// ----------------------------------------------------

SuperDial clockFace(&lcd, 5, 5, 150, /*body height*/ 150, &lgfx::fonts::Font2);
SuperDial compass(&lcd, 165, 5, 150, 150, &lgfx::fonts::Font2);
SuperDial tacho(&lcd, 5, 175, 200, 140, &lgfx::fonts::Font2);
SuperForm form(&Enc, &BtnA);

// BeforeDraw callbacks: the dial calls these itself (every SetPollMs() ms) and redraws only when they changed something.
// This is where the code that reads your clock / sensors belongs; nothing is drawn while they run.
void clockBefore(void*) {
  uint32_t s = millis() / 1000 + 10UL * 3600 + 8 * 60;               // starts at 10:08:00 (use your RTC here)
  clockFace.SetTime((int)(s / 3600 % 24), (int)(s / 60 % 60), (int)(s % 60));
}
void compassBefore(void*) { compass.SetHeading(fmodf(millis() * 0.01f, 360.f)); }       // the card turns, by the short way round
void tachoBefore(void*)   { tacho.SetPointerValue(0, 4.f + 3.5f * sinf(millis() * 0.0009f)); }

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.fillScreen(TFT_BLACK);
  Enc.Begin(); BtnA.Begin(INPUT_PULLUP); BtnB.Begin(INPUT_PULLUP);

  clockFace.SetBorder(1, TFT_WHITE);  clockFace.SetLabel("Clock");
  clockFace.MakeClock();                            // 12 hour face: three pointers (hours, minutes, seconds)
  clockFace.SetSmoothing(false);                    // a clock jumps to the new time (leave smoothing on for a sweeping second hand)
  clockFace.SetOnBeforeDraw(clockBefore);  clockFace.SetPollMs(1000);        // asked once a second

  compass.SetBorder(1, TFT_WHITE);  compass.SetLabel("Compass");
  compass.MakeCompass();                        // N E S W card; the red pointer stays at the top
  compass.AddPointer(0.6f, 3, TFT_CYAN, SuperDial::POINTER_LINE, /*faceRelative*/ true);   // a bearing marker that turns with the card
  compass.SetPointerValue(1, 90);               // ... fixed at east on the card
  compass.AddZone(300, 359, TFT_RED);           // a no-go sector on the card
  compass.SetHeading(0);
  compass.SetOnBeforeDraw(compassBefore);  compass.SetPollMs(400);

  tacho.SetBorder(1, TFT_WHITE);  tacho.SetLabel("Engine");
  tacho.SetSweep(0, 8, -135, 135);              // 0 .. 8 (x1000 rpm) over 270 degrees
  tacho.SetTicks(8, 4);
  tacho.AddZone(0, 6, TFT_GREEN);  tacho.AddZone(6, 7, TFT_YELLOW);  tacho.AddZone(7, 8, TFT_RED);
  tacho.AddPointer(0.9f, 6, TFT_WHITE);         // pointer 0 (the hub shows its value)
  tacho.SetFormat(1, " k");
  tacho.SetOnBeforeDraw(tachoBefore);  tacho.SetPollMs(400);

  form.Add(&clockFace, false);                      // display only: never focused
  form.Add(&compass, false);
  form.Add(&tacho, false);
  form.Draw();
}

void loop() {
  form.Service();                               // drives the glides and calls the BeforeDraw callbacks; without a form call clockFace.Update() / compass.Update() / tacho.Update() here
}
