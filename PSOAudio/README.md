# PSOAudio

PSOAudio is a shared Arduino audio library for PSO-style sound effects and prop projects.

It provides:

- A common public API in `PSOAudio.h`
- Generated sound assets in `src/Sounds`
- A FIFO sound queue
- Interruptible playback
- RP2040 I2S playback
- nRF52840 PWM/TIMER1 playback
- Cross-core queue protection on RP2040
- Playback-start counters for synchronizing LED and other effects

PSOAudio is independent of `PSOEffects`. Projects that need NeoPixel effects can include both libraries:

```cpp
#include <PSOEffects.h>
#include <PSOAudio.h>
```

## Supported platforms

| Platform | Audio backend | Playback service | Important notes |
|---|---|---|---|
| RP2040 | I2S | `loop1()` on core 1 | Audio is queued by `loop()`/core 0 and played by `loop1()`/core 1. |
| nRF52840 | PWM using TIMER1 | Main `loop()` | TIMER1 is reserved by PSOAudio. Do not use TIMER1 for another subsystem. |

The RP2040 backend requires an RP2040 Arduino core that provides the Pico SDK mutex API used by `PSOAudio.cpp`.

## Installation

### Manual installation

Copy the complete library folder into the Arduino sketchbook libraries directory:

```text
Arduino/
└── libraries/
    └── PSOAudio/
        ├── library.properties
        ├── README.md
        └── src/
            ├── PSOAudio.h
            ├── PSOAudio.cpp
            └── Sounds/
                ├── blast.h
                ├── Charging.h
                ├── ChargeDone.h
                ├── ErrorSound1.h
                ├── HPBeams.h
                ├── InitIntro.h
                ├── InitOutro.h
                ├── power_sound.h
                ├── swap_sound.h
                ├── powerup.h
                ├── mechBlast.h
                └── rifleBlast.h
```

Typical sketchbook locations are:

```text
Windows:
C:\Users\<username>\Documents\Arduino\libraries\PSOAudio

macOS:
~/Documents/Arduino/libraries/PSOAudio

Linux:
~/Arduino/libraries/PSOAudio
```

### ZIP installation

The ZIP must contain one top-level `PSOAudio` folder:

```text
PSOAudio.zip
└── PSOAudio/
    ├── library.properties
    ├── README.md
    └── src/
        ├── PSOAudio.h
        ├── PSOAudio.cpp
        └── Sounds/
```

Do not create a ZIP with `src/` as the top-level directory.

A minimal `library.properties` file is:

```ini
name=PSOAudio
version=1.0.0
author=AW Labs
maintainer=AW Labs
sentence=Shared RP2040 and nRF52840 audio playback
paragraph=Provides sound assets, queue handling, interruptible playback, RP2040 I2S output, and nRF52840 PWM output.
category=Device Control
url=
architectures=*
includes=PSOAudio.h
```

After installing or updating the library, restart the Arduino IDE.

Include the library normally:

```cpp
#include <PSOAudio.h>
```

Do not include it as:

```cpp
#include <src/PSOAudio.h>
```

## Source layout

### `PSOAudio.h`

`PSOAudio.h` contains the public interface:

- `PSOAudio::SoundId`
- `PSOAudio::N_SOUNDS`
- `PSOAudio::Pins`
- `PSOAudio::makeI2SPins()`
- `PSOAudio::makePWMPins()`
- `PSOAudio::begin()`
- `PSOAudio::queueSound()`
- `PSOAudio::startClip()`
- `PSOAudio::interruptAndClearQueue()`
- `PSOAudio::serviceAudioQueue()`
- `PSOAudio::isPlaying()`
- `PSOAudio::startCount()`

### `PSOAudio.cpp`

`PSOAudio.cpp` contains:

- The sound table
- The shared circular queue
- RP2040 queue locking
- The RP2040 I2S backend
- The nRF52840 PWM/TIMER1 backend
- Playback generation tracking
- Playback-start counters

### `src/Sounds`

The files in `src/Sounds` are generated sound headers. They contain the sample arrays, lengths, and sample rates used by the table in `PSOAudio.cpp`.

## Supported sound IDs

The available IDs are:

| ID | Typical use |
|---|---|
| `SND_INIT_INTRO` | Startup animation |
| `SND_INIT_OUTRO` | Startup completion animation |
| `SND_BLAST` | Handgun shot |
| `SND_CHARGING` | Charging or power-up loop |
| `SND_CHARGED` | Charging completed |
| `SND_HPBEAMS` | Beam or special attack |
| `SND_DENIED` | Invalid or unavailable action |
| `SND_POWER_ON` | Power-on chime |
| `SND_SWAP` | Mode or weapon change |
| `SND_POWERUP` | Weapon power-up sequence |
| `SND_MECH_BLAST` | Mech gun shot |
| `SND_RIFLE` | Rifle shot |

`N_SOUNDS` is a count/sentinel value. Do not pass `N_SOUNDS` to the playback functions.

## RP2040 quick start

On the RP2040, audio playback should run on core 1. The main loop can queue sounds from core 0.

For an Adafruit Prop-Maker-style board:

```cpp
#include <PSOAudio.h>

void setup() {
  Serial.begin(115200);

  // Enable the external audio circuit.
  pinMode(PIN_EXTERNAL_POWER, OUTPUT);
  digitalWrite(PIN_EXTERNAL_POWER, HIGH);

  // Initialize the external button and other project hardware here.
  pinMode(PIN_EXTERNAL_BUTTON, INPUT_PULLUP);
}

void setup1() {
  // setup() and setup1() may run concurrently on the two RP2040 cores.
  // Set the amplifier enable pin here as well so audio cannot begin before
  // the external power circuit is enabled.
  pinMode(PIN_EXTERNAL_POWER, OUTPUT);
  digitalWrite(PIN_EXTERNAL_POWER, HIGH);

  const PSOAudio::Pins audioPins =
    PSOAudio::makeI2SPins(
      PIN_I2S_BIT_CLOCK,
      PIN_I2S_DATA
    );

  if (!PSOAudio::begin(audioPins)) {
    Serial.println("PSOAudio initialization failed");
    return;
  }

  // Allow the amplifier and filtering circuitry to settle.
  delay(100);

  PSOAudio::startClip(PSOAudio::SND_POWER_ON);
}

void loop1() {
  // This must run continuously on core 1.
  PSOAudio::serviceAudioQueue();
}

void loop() {
  // Example:
  // PSOAudio::startClip(PSOAudio::SND_BLAST);
}
```

`setup1()` and `loop1()` are required for the RP2040 audio backend. Do not remove `loop1()` or replace it with a long blocking delay.

### RP2040 I2S pins

The I2S pins are supplied by the board variant:

```cpp
PSOAudio::makeI2SPins(
  PIN_I2S_BIT_CLOCK,
  PIN_I2S_DATA
);
```

The exact pin numbers depend on the selected board variant. The I2S data and clock signals must be connected to the audio amplifier according to the board wiring diagram.

### RP2040 amplifier power

PSOAudio does not automatically control an external amplifier’s enable or power pin. The sketch must:

1. Configure the amplifier power pin.
2. Set it to the active level.
3. Allow a short settling period.
4. Start the audio clip.

A 100 ms delay is a suitable starting point, although the correct delay depends on the amplifier and filtering circuit.

## nRF52840 quick start

The nRF52840 backend uses a PWM audio output and TIMER1. Audio is serviced from the main loop:

```cpp
#include <PSOAudio.h>

constexpr int AUDIO_PIN = 2; // Use the project’s actual audio pin.

void setup() {
  Serial.begin(115200);

  const PSOAudio::Pins audioPins =
    PSOAudio::makePWMPins(AUDIO_PIN);

  if (!PSOAudio::begin(audioPins)) {
    Serial.println("PSOAudio initialization failed");
    return;
  }

  PSOAudio::startClip(PSOAudio::SND_POWER_ON);
}

void loop() {
  PSOAudio::serviceAudioQueue();

  // Project logic, buttons, LEDs, and effects can run here.
}
```

Do not define or use `setup1()` or `loop1()` for the nRF52840 backend.

### nRF52840 timing

The nRF52840 backend configures TIMER1 using a 1 MHz timer tick. It calculates a timer period for each clip's sample rate.

PSOAudio reserves TIMER1. Do not use TIMER1 independently elsewhere in the project.

The current compatibility backend uses `analogWrite()` from the timer interrupt. This is retained for compatibility with the original scaffold, but a hardware PWM or DMA implementation may provide more consistent timing and lower CPU overhead.

## API reference

### `PSOAudio::Pins`

Represents the platform-specific audio pins.

Use the factory functions rather than manually constructing the structure:

```cpp
PSOAudio::makeI2SPins(bclkPin, dataPin);
PSOAudio::makePWMPins(pwmPin);
```

### `bool begin(const Pins &pins)`

Initializes the platform audio backend.

Returns:

- `true` when the backend was configured successfully
- `false` when the pin configuration is invalid or backend initialization fails

Call `begin()` once during setup, before starting sounds.

### `void queueSound(SoundId id)`

Adds one sound to the end of the FIFO queue.

The queue holds eight entries. If the queue is full, the new sound is dropped instead of blocking the project.

Use this when sounds should play in sequence:

```cpp
PSOAudio::queueSound(PSOAudio::SND_POWERUP);
PSOAudio::queueSound(PSOAudio::SND_BLAST);
```

### `void startClip(SoundId id)`

Interrupts the currently playing clip, clears the pending queue, and queues the selected sound.

```cpp
PSOAudio::startClip(PSOAudio::SND_BLAST);
```

This is the normal method for a new trigger, button, or weapon event.

On RP2040, the currently playing clip checks the playback generation between samples and stops when interrupted. On nRF52840, the active timer-backed clip is stopped.

### `void interruptAndClearQueue()`

Stops current playback and removes all queued sounds.

```cpp
PSOAudio::interruptAndClearQueue();
```

This function increments the playback generation so a clip that has already been removed from the queue cannot begin later.

### `void serviceAudioQueue()`

Services the audio backend.

### RP2040

Call it continuously from `loop1()`:

```cpp
void loop1() {
  PSOAudio::serviceAudioQueue();
}
```

### nRF52840

Call it from the main `loop()`:

```cpp
void loop() {
  PSOAudio::serviceAudioQueue();
}
```

### `bool isPlaying()`

Returns `true` when:

- A sound is waiting in the queue, or
- The platform backend is actively playing a sound

This is intended for status polling. It should not be used as a synchronization primitive.

### `uint16_t startCount(SoundId id)`

Returns the number of times playback of the selected sound has genuinely started.

The counter is incremented only after the backend has started the clip. It is useful for synchronizing visual effects with actual audio playback.

Example:

```cpp
static uint16_t lastBlastCount = 0;

void updateBlastFlash() {
  const uint16_t currentCount =
    PSOAudio::startCount(PSOAudio::SND_BLAST);

  while (lastBlastCount != currentCount) {
    // A new blast has started.
    startMuzzleFlash();

    lastBlastCount++;
  }
}
```

The counter is a `uint16_t` and naturally wraps after 65,535 starts. The comparison above remains valid across normal wraparound.

## Sound asset format

The current backend expects generated sound headers to provide:

1. An array of unsigned 8-bit samples
2. A sample count
3. A sample rate

For example, `blast.h` is expected to provide values equivalent to:

```cpp
BLAST_SOUND
BLAST_SOUND_LENGTH
BLAST_SOUND_SAMPLE_RATE
```

The backend treats each sample as unsigned 8-bit PCM with a center value of approximately `128`:

```cpp
int16_t centered = (int16_t)sample - 128;
```

The sample is then scaled for the selected backend.

Do not replace the generated arrays with signed 16-bit PCM without modifying both the `SoundClip` structure and the playback backend.

## Adding a new sound

To add a sound:

1. Generate the sound header and place it in `src/Sounds/`.
2. Include the new header in `PSOAudio.cpp`.
3. Add a new `SoundId` to `PSOAudio.h`.
4. Add a matching table entry to `clips[]` in `PSOAudio.cpp`.
5. Keep the enum and table entries in exactly the same order.
6. Recompile the library.

Example header include:

```cpp
#include "Sounds/mySound.h"
```

Example enum entry:

```cpp
SND_MY_SOUND,
N_SOUNDS
```

Example table entry:

```cpp
// SND_MY_SOUND
{
  MY_SOUND,
  MY_SOUND_LENGTH,
  MY_SOUND_SAMPLE_RATE
},
```

The `static_assert` in `PSOAudio.cpp` catches a mismatch in the number of sound entries, but it cannot detect an incorrect ordering. The order of the enum and the table must be maintained manually.

## Threading and synchronization

### RP2040

The intended arrangement is:

```text
Core 0                         Core 1
-------                        -------
setup()                        setup1()
loop()                         loop1()
                               PSOAudio::serviceAudioQueue()
```

Core 0 may call queue APIs such as:

```cpp
PSOAudio::startClip(...);
PSOAudio::queueSound(...);
PSOAudio::interruptAndClearQueue();
```

Core 1 owns I2S playback and must keep calling:

```cpp
PSOAudio::serviceAudioQueue();
```

The queue uses the Pico SDK mutex to protect queue state across cores. Do not replace the mutex with older or incompatible spinlock calls such as:

```cpp
spin_lock_unsafe(...)
spin_lock_unsafe_enter(...)
spin_lock_unsafe_exit(...)
```

Those APIs are not compatible with the current RP2040 Arduino core/library build.

### nRF52840

The intended arrangement is single-core:

```text
setup()
loop()
  └── PSOAudio::serviceAudioQueue()
```

The timer interrupt consumes the active sample but does not access the sound queue.

### General rules

- Call audio queue APIs from normal setup/loop code, not from an interrupt.
- Do not call `serviceAudioQueue()` from both cores on RP2040.
- Do not put long delays in `loop1()`.
- Keep audio pins and amplifier power initialization in the sketch.
- Do not use PSOAudio's nRF52840 TIMER1 for another timer purpose.

## Troubleshooting

### No sound on boot

Check the following:

1. `PSOAudio::begin()` is being called.
2. `begin()` returns `true`.
3. `setup1()` and `loop1()` exist on RP2040.
4. The amplifier enable/power pin is set to its active level.
5. There is a settling delay after enabling the amplifier.
6. The I2S BCLK and DATA pins match the selected board variant.
7. The amplifier and RP2040 share a common ground.
8. The selected sound header has nonzero length and sample-rate values.
9. The volume or amplifier gain is not set to zero.

A useful diagnostic is to monitor the playback-start counter:

```cpp
Serial.println(
  PSOAudio::startCount(PSOAudio::SND_POWER_ON)
);
```

If the counter does not increase, the clip has not started. If it increases but there is still no audible sound, investigate the I2S wiring, amplifier enable, power, grounding, and volume.

### Trigger or mode button freezes the board

The current RP2040 implementation uses a Pico SDK mutex. If a freeze returns after modifying the library:

- Confirm that `pico/mutex.h` is included.
- Confirm that `mutex_enter_blocking()` and `mutex_exit()` are used.
- Remove any leftover custom `LDREX`/`STREX` implementation.
- Remove any remaining `spin_lock_unsafe*` calls.
- Make sure `loop1()` continues calling `PSOAudio::serviceAudioQueue()`.
- Make sure queue APIs are not being called from an interrupt.
- Remove duplicate or older copies of `PSOAudio` from the sketchbook libraries directory.

### Sound starts but the prop freezes

Check that:

- The audio service loop is running on the correct core.
- The RP2040 queue is not being accessed by two service loops.
- No long blocking operation is running inside `loop1()`.
- The amplifier is not being reset repeatedly.
- The selected sound length and sample rate are valid.

### Old library code is still being compiled

Arduino may continue using a cached library object after a library source change.

1. Save all files.
2. Close the Arduino IDE.
3. Reopen the IDE.
4. Compile again.
5. If necessary, delete the generated build-cache directory shown in the compiler output.

Also check that only one version of `PSOAudio` exists in the active sketchbook `libraries` directory.

## License

MIT
