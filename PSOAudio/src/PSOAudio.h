#pragma once

#include <Arduino.h>

namespace PSOAudio {

// Sound table order. The SoundId values must match the table in
// PSOAudio.cpp exactly.
enum SoundId : uint8_t {
  // Heavens Punisher
  SND_INIT_INTRO = 0,
  SND_INIT_OUTRO,
  SND_BLAST,
  SND_CHARGING,
  SND_CHARGED,
  SND_HPBEAMS,
  SND_DENIED,

  // Prop-Maker blaster
  SND_POWER_ON,
  SND_SWAP,
  SND_POWERUP,
  SND_MECH_BLAST,
  SND_RIFLE,

  N_SOUNDS
};

// Pin description. Only the fields relevant to the selected platform
// are used:
//
// RP2040:
//   bclkPin = I2S bit-clock pin
//   dataPin = I2S data pin
//   (makeI2SPins: Feather RP2040 Prop-Maker)
//   -- or --
//   pwmPin  = PWM audio output pin, when the amp takes a PWM/analog
//   input (makePWMPins: e.g. Photon Drop on a XIAO RP2040)
//
// nRF52840:
//   pwmPin = audio PWM output pin
struct Pins {
  int pwmPin;
  int bclkPin;
  int dataPin;
};

inline Pins makePWMPins(int pin) {
  Pins pins;
  pins.pwmPin = pin;
  pins.bclkPin = -1;
  pins.dataPin = -1;
  return pins;
}

inline Pins makeI2SPins(int bitClockPin, int dataPin) {
  Pins pins;
  pins.pwmPin = -1;
  pins.bclkPin = bitClockPin;
  pins.dataPin = dataPin;
  return pins;
}

// Initialize the platform-specific audio hardware.
//
// RP2040:
//   Call from setup1(). The I2S object itself is not started until a
//   clip is played.
//
// nRF52840:
//   Call from setup().
bool begin(const Pins &pins);

// Interrupt the current clip, clear all queued clips, and queue id.
//
// On RP2040, playback begins when loop1() calls serviceAudioQueue().
// On nRF52840, playback begins immediately.
void startClip(SoundId id);

// Add a clip without interrupting the currently playing clip.
//
// This is used for sequences such as:
//   powerup -> shot
void queueSound(SoundId id);

// Interrupt the current clip and remove all queued clips.
void interruptAndClearQueue();

// Service pending audio.
//
// RP2040: call from loop1(), not loop().
// nRF52840: call from loop().
void serviceAudioQueue();

// True if a clip is playing or waiting to play.
bool isPlaying();

// Increments when a clip actually begins, rather than when it is queued.
//
// This is used on RP2040 to synchronize muzzle flashes to the actual
// start of a sound.
uint16_t startCount(SoundId id);

} // namespace PSOAudio