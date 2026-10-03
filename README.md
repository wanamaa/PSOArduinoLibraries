# PSOEffects

A tiny, header-only Arduino library that gives every prop the same LED "feel": a fixed six-color palette, a rainbow mode, and a smooth linear breathing effect for NeoPixel (WS2812-style) strips.

Write the color logic once, `#include` it in each project, and your props all glow the same way.

## Features

- **Six fixed colors:** green, blue, magenta, red, orange, white
- **Rainbow mode:** a flowing, gamma-corrected hue sweep along the strip
- **Linear breathing:** triangle-wave brightness, so the glow fades up and down at a steady rate
- **Per-channel RGB scaling:** brightness is applied to each color channel, so hues stay true as they dim
- **One-call dispatcher:** pass a mode index and it picks solid color or rainbow for you
- **Header-only:** no `.cpp` files, nothing to build

## Requirements

- An Arduino-compatible board
- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) library. A recent version is needed, since the rainbow mode uses `gamma32()`.

## Installation

1. Download or clone this repository.
2. Place `PSOEffects.h` in your Arduino libraries folder:

   ```
   ~/Documents/Arduino/libraries/PSOEffects/PSOEffects.h
   ```

3. Restart the Arduino IDE.
4. Include it in your sketch (after or alongside the NeoPixel include):

   ```cpp
   #include <Adafruit_NeoPixel.h>
   #include <PSOEffects.h>
   ```

## Quick start

This sketch cycles through all seven modes each time a button is pressed.

```cpp
#include <Adafruit_NeoPixel.h>
#include <PSOEffects.h>

#define LED_PIN     6
#define LED_COUNT   16
#define BUTTON_PIN  2

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t currentMode = 0;
bool lastButton = HIGH;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  strip.begin();
  strip.show();
}

void loop() {
  // Button press (active low) advances to the next mode
  bool button = digitalRead(BUTTON_PIN);
  if (lastButton == HIGH && button == LOW) {
    currentMode = (currentMode + 1) % PSO_NUM_MODES;
    delay(30); // crude debounce
  }
  lastButton = button;

  psoUpdateBreathingColor(strip, millis(), currentMode);
}
```

## Modes

Modes are indexed `0` to `PSO_NUM_MODES - 1`:

| Index | Mode    | RGB             |
|-------|---------|-----------------|
| 0     | Green   | `0, 255, 0`     |
| 1     | Blue    | `0, 0, 255`     |
| 2     | Magenta | `255, 0, 255`   |
| 3     | Red     | `255, 0, 0`     |
| 4     | Orange  | `255, 106, 0`   |
| 5     | White   | `255, 255, 255` |
| 6     | Rainbow | hue sweep       |

## API reference

### Functions

#### `psoUpdateBreathingColor(strip, now, currentMode)`

The main entry point. Computes the current breathing brightness and draws either a solid color or the rainbow, then calls `strip.show()`. Call it once per `loop()` iteration.

| Parameter     | Type                   | Description                                      |
|---------------|------------------------|--------------------------------------------------|
| `strip`       | `Adafruit_NeoPixel &`  | The strip to draw to                             |
| `now`         | `uint32_t`             | Current time in ms, usually `millis()`           |
| `currentMode` | `uint8_t`              | Mode index, `0` to `PSO_NUM_MODES - 1`           |

#### `psoBreathBrightness(now)`

Returns the current breathing brightness (`PSO_BREATH_MIN` to `PSO_BREATH_MAX`) as a triangle wave over `PSO_BREATH_PERIOD_MS`. Useful if you want to drive your own effects from the same breathing cycle.

#### `psoUpdateSolidColor(strip, color, brightness)`

Fills the whole strip with one `RGB` color, scaled per channel by `brightness` (0-255), and calls `strip.show()`.

#### `psoUpdateRainbow(strip, now, brightness)`

Draws a moving rainbow across the strip at the given `brightness` and calls `strip.show()`. The pattern is gamma-corrected, and the hue wraps `PSO_RAINBOW_SPAN` times along the strip.

### Types and constants

| Name                      | Description                                           | Default |
|---------------------------|-------------------------------------------------------|---------|
| `RGB`                     | `struct { uint8_t r, g, b; }`                         | n/a     |
| `PSO_COLORS[]`            | The six-color palette                                 | n/a     |
| `PSO_NUM_COLORS`          | Number of solid colors                                | `6`     |
| `PSO_RAINBOW_MODE_INDEX`  | Mode index that selects rainbow                       | `6`     |
| `PSO_NUM_MODES`           | Total modes (colors + rainbow)                        | `7`     |
| `PSO_MODE_NAMES[]`        | Human-readable name for each mode, e.g. for a display or serial log | n/a |
| `PSO_BREATH_MIN`          | Minimum breathing brightness (0-255)                  | `15`    |
| `PSO_BREATH_MAX`          | Maximum breathing brightness (0-255)                  | `180`   |
| `PSO_BREATH_PERIOD_MS`    | Full breathing cycle length                           | `3000`  |
| `PSO_RAINBOW_CYCLE_MS`    | Time for the rainbow to complete one full cycle       | `6000`  |
| `PSO_RAINBOW_SPAN`        | Number of full hue cycles spread across the strip     | `3`     |

## Customizing

All tuning values are plain constants at the top of `PSOEffects.h`. Edit them to change the look for every project that uses the library:

- Slower or faster breathing: change `PSO_BREATH_PERIOD_MS`
- Dimmer or brighter range: change `PSO_BREATH_MIN` / `PSO_BREATH_MAX`
- Different rainbow density or speed: change `PSO_RAINBOW_SPAN` / `PSO_RAINBOW_CYCLE_MS`
- New palette: edit `PSO_COLORS[]` and `PSO_MODE_NAMES[]` together, keeping them the same length and order

## Notes

- **Brightness is handled by the library.** Don't also call `strip.setBrightness()`, or the two scalings will stack and dim the output.
- **Each update call ends with `strip.show()`.** Avoid calling `show()` again in the same loop pass.
- **Keep `loop()` non-blocking.** Breathing is computed from `millis()`, so long `delay()` calls will make the animation stutter.
- **Timing is based on `millis()`.** After about 49 days of continuous uptime the counter wraps, which can cause a single small jump in the animation phase. It is harmless for typical prop use.

## License

Add your license here (for example, MIT).
