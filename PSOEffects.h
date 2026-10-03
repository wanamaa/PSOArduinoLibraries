#pragma once
#include <Adafruit_NeoPixel.h>

// ---------------- Shared color/breathing scheme ----------------
// Same palette + math established on the dagger: a fixed 6-color set plus
// a rainbow mode, linear (triangle-wave) breathing, per-channel RGB
// scaling. Every prop that includes this gets the same "feel" for free.
//
// Install: drop this file at
//   ~/Documents/Arduino/libraries/PSOEffects/PSOEffects.h
// then any sketch can `#include <PSOEffects.h>` after an IDE restart.

struct RGB { uint8_t r, g, b; };

const RGB PSO_COLORS[] = {
  {0,   255, 0},   // green
  {0,   0,   255}, // blue
  {255, 0,   255}, // magenta
  {255, 0,   0},   // red
  {255, 106, 0},   // orange
  {255, 255, 255}, // white
};
const uint8_t PSO_NUM_COLORS         = sizeof(PSO_COLORS) / sizeof(PSO_COLORS[0]);
const uint8_t PSO_RAINBOW_MODE_INDEX = PSO_NUM_COLORS;
const uint8_t PSO_NUM_MODES          = PSO_NUM_COLORS + 1;

const char *PSO_MODE_NAMES[PSO_NUM_MODES] = {
  "Green", "Blue", "Magenta", "Red", "Orange", "White", "Rainbow"
};

const uint8_t  PSO_BREATH_MIN       = 15;
const uint8_t  PSO_BREATH_MAX       = 180;
const uint16_t PSO_BREATH_PERIOD_MS = 3000;

const uint16_t PSO_RAINBOW_CYCLE_MS = 6000;
const uint16_t PSO_RAINBOW_SPAN     = 3;

// `inline` matters here: this header may end up included from more than
// one .cpp/.ino translation unit in a sketch someday, and inline avoids
// "multiple definition" linker errors if that happens.

inline uint8_t psoBreathBrightness(uint32_t now) {
  long t = (long)(now % PSO_BREATH_PERIOD_MS);
  long halfPeriod = PSO_BREATH_PERIOD_MS / 2;
  if (t < halfPeriod) {
    return map(t, 0, halfPeriod, PSO_BREATH_MIN, PSO_BREATH_MAX);
  } else {
    return map(t - halfPeriod, 0, halfPeriod, PSO_BREATH_MAX, PSO_BREATH_MIN);
  }
}

inline void psoUpdateSolidColor(Adafruit_NeoPixel &strip, RGB c, uint8_t brightness) {
  uint8_t sr = (uint16_t)c.r * brightness / 255;
  uint8_t sg = (uint16_t)c.g * brightness / 255;
  uint8_t sb = (uint16_t)c.b * brightness / 255;
  uint32_t scaled = strip.Color(sr, sg, sb);
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, scaled);
  }
  strip.show();
}

inline void psoUpdateRainbow(Adafruit_NeoPixel &strip, uint32_t now, uint8_t brightness) {
  long t = (long)(now % PSO_RAINBOW_CYCLE_MS);
  uint16_t baseHue = map(t, 0, PSO_RAINBOW_CYCLE_MS, 0, 65535);

  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    uint16_t pixelHue = baseHue + (uint32_t)(65535UL * PSO_RAINBOW_SPAN * i / strip.numPixels());
    uint32_t fullColor = strip.gamma32(strip.ColorHSV(pixelHue, 255, 255));

    uint8_t r = (uint8_t)(fullColor >> 16);
    uint8_t g = (uint8_t)(fullColor >> 8);
    uint8_t b = (uint8_t)(fullColor);

    uint8_t sr = (uint16_t)r * brightness / 255;
    uint8_t sg = (uint16_t)g * brightness / 255;
    uint8_t sb = (uint16_t)b * brightness / 255;

    strip.setPixelColor(i, strip.Color(sr, sg, sb));
  }
  strip.show();
}

// One-call dispatcher: pass the current mode index (0..PSO_NUM_MODES-1)
// and this picks solid-color vs rainbow automatically.
inline void psoUpdateBreathingColor(Adafruit_NeoPixel &strip, uint32_t now, uint8_t currentMode) {
  uint8_t brightness = psoBreathBrightness(now);
  if (currentMode == PSO_RAINBOW_MODE_INDEX) {
    psoUpdateRainbow(strip, now, brightness);
  } else {
    psoUpdateSolidColor(strip, PSO_COLORS[currentMode], brightness);
  }
}
