#include "PSOAudio.h"

// All generated sound headers are included once, here. They must be
// available to the library, preferably from PSOAudio/src/Sounds.
#include "Sounds/blast.h"
#include "Sounds/Charging.h"
#include "Sounds/ChargeDone.h"
#include "Sounds/ErrorSound1.h"
#include "Sounds/HPBeams.h"
#include "Sounds/InitIntro.h"
#include "Sounds/InitOutro.h"

#include "Sounds/power_sound.h"
#include "Sounds/swap_sound.h"
#include "Sounds/powerup.h"
#include "Sounds/mechBlast.h"
#include "Sounds/rifleBlast.h"

#if defined(ARDUINO_ARCH_RP2040)

  #include <I2S.h>
  #include <pico/mutex.h>

  #define PSOAUDIO_RP2040 1
  #define PSOAUDIO_NRF52840 0

#elif defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_ARCH_NRF52840)

  #include <nrf.h>

  #define PSOAUDIO_RP2040 0
  #define PSOAUDIO_NRF52840 1

#else
  #error "PSOAudio currently supports only RP2040 and nRF52840"
#endif

namespace {

struct SoundClip {
  const uint8_t *data;
  uint32_t len;
  uint32_t rate;
};

constexpr uint8_t QUEUE_LENGTH = 8;

// This order must exactly match PSOAudio::SoundId.
const SoundClip clips[PSOAudio::N_SOUNDS] = {
  // SND_INIT_INTRO
  {
    INIT_INTRO_SOUND,
    INIT_INTRO_SOUND_LENGTH,
    INIT_INTRO_SOUND_SAMPLE_RATE
  },

  // SND_INIT_OUTRO
  {
    INIT_OUTRO_SOUND,
    INIT_OUTRO_SOUND_LENGTH,
    INIT_OUTRO_SOUND_SAMPLE_RATE
  },

  // SND_BLAST
  {
    BLAST_SOUND,
    BLAST_SOUND_LENGTH,
    BLAST_SOUND_SAMPLE_RATE
  },

  // SND_CHARGING
  {
    CHARGING_SOUND,
    CHARGING_SOUND_LENGTH,
    CHARGING_SOUND_SAMPLE_RATE
  },

  // SND_CHARGED
  {
    CHARGE_DONE_SOUND,
    CHARGE_DONE_SOUND_LENGTH,
    CHARGE_DONE_SOUND_SAMPLE_RATE
  },

  // SND_HPBEAMS
  {
    BEAMS_SOUND,
    BEAMS_SOUND_LENGTH,
    BEAMS_SOUND_SAMPLE_RATE
  },

  // SND_DENIED
  {
    ERROR_1_SOUND,
    ERROR_1_SOUND_LENGTH,
    ERROR_1_SOUND_SAMPLE_RATE
  },

  // SND_POWER_ON
  {
    POWER_SOUND,
    POWER_SOUND_LENGTH,
    POWER_SOUND_SAMPLE_RATE
  },

  // SND_SWAP
  {
    SWAP_SOUND,
    SWAP_SOUND_LENGTH,
    SWAP_SOUND_SAMPLE_RATE
  },

  // SND_POWERUP
  {
    POWERUP_SOUND,
    POWERUP_SOUND_LENGTH,
    POWERUP_SOUND_SAMPLE_RATE
  },

  // SND_MECH_BLAST
  {
    MECHBLAST_SOUND,
    MECHBLAST_SOUND_LENGTH,
    MECHBLAST_SOUND_SAMPLE_RATE
  },

  // SND_RIFLE
  {
    RIFLEBLAST_SOUND,
    RIFLEBLAST_SOUND_LENGTH,
    RIFLEBLAST_SOUND_SAMPLE_RATE
  }
};

static_assert(
  sizeof(clips) / sizeof(clips[0]) == PSOAudio::N_SOUNDS,
  "Sound table does not match PSOAudio::SoundId"
);

// ---------------------------------------------------------------------------
// Shared queue
// ---------------------------------------------------------------------------
//
// On RP2040, loop()/core0 produces queue entries while loop1()/core1
// consumes them. All queue accesses are protected by the SDK spinlock.
//
// On nRF52840, the application is single-core. The timer ISR does not
// access the queue, so the lock functions are no-ops.
// ---------------------------------------------------------------------------

volatile uint8_t soundQueue[QUEUE_LENGTH];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;
volatile uint8_t queueCount = 0;

// Bumped whenever playback should be interrupted. The RP2040 I2S loop
// checks this between samples.
volatile uint16_t playGeneration = 0;

// One counter per sound. These are incremented only after playback has
// genuinely started.
volatile uint16_t soundStartCount[PSOAudio::N_SOUNDS] = {};

#if PSOAUDIO_RP2040

// The RP2040 queue is shared by core0 and core1. Use the Pico SDK's
// mutex, which is designed for cross-core blocking synchronization.
//
// The previous pointer-based software spinlock could stall when both
// cores contended for the queue.
mutex_t queueMutex;

// Constructors in library .cpp files run before Arduino starts its
// setup functions and launches the second core.
struct QueueMutexInitializer {
  QueueMutexInitializer() {
    mutex_init(&queueMutex);
  }
};

QueueMutexInitializer queueMutexInitializer;

void lockQueue() {
  mutex_enter_blocking(&queueMutex);
}

void unlockQueue() {
  mutex_exit(&queueMutex);
}

#else

// The nRF52840 queue is accessed only from loop(). The TIMER1 ISR
// consumes the active sample but never accesses the queue.
void lockQueue() {
}

void unlockQueue() {
}

#endif // PSOAUDIO_RP2040


bool popSound(uint8_t &id, uint16_t &generation) {
  lockQueue();

  if (queueCount == 0) {
    unlockQueue();
    return false;
  }

  id = soundQueue[queueTail];

  queueTail = (queueTail + 1) % QUEUE_LENGTH;
  queueCount--;

  generation = playGeneration;

  unlockQueue();
  return true;
}

// ---------------------------------------------------------------------------
// RP2040 I2S backend
// ---------------------------------------------------------------------------
#if PSOAUDIO_RP2040

I2S i2sDevice(OUTPUT);

bool i2sReady = false;
volatile bool core1Busy = false;

// PWM output mode, for boards whose amplifier takes a PWM/analog input
// (e.g. Photon Drop's PAM8302 on a XIAO RP2040). Selected in begin() by
// passing makePWMPins(). I2S mode (Adafruit Feather RP2040 Prop-Maker)
// is selected by passing makeI2SPins() and is unchanged.
bool pwmReady = false;
int pwmAudioPin = -1;

// PWM carrier well above the audio band and above the ~16 kHz corner of
// the Photon Drop input integrator. analogWriteFreq()/analogWriteRange()
// apply to all PWM outputs on the RP2040.
constexpr uint32_t PWM_CARRIER_HZ = 62500UL;
constexpr uint16_t PWM_RANGE = 256;

// Same gain as the nRF52840 backend: centered * 4 / 32 = centered / 8.
constexpr int32_t PWM_GAIN_NUMERATOR = 4;
constexpr int32_t PWM_GAIN_DENOMINATOR = 32;

// Plays one clip on core1 by writing a PWM duty per sample, paced by
// micros(). Core1 has nothing else to do, so a paced loop is fine.
// Returns after the clip ends or playGeneration changes.
void rp2040PwmPlay(const SoundClip &clip, uint16_t generation) {
  const uint32_t start = micros();

  for (uint32_t i = 0; i < clip.len; i++) {
    if (generation != playGeneration) {
      break;
    }

    // Absolute schedule (no drift), valid for any sample rate.
    const uint32_t due =
      (uint32_t)(((uint64_t)i * 1000000ULL) / clip.rate);

    while ((uint32_t)(micros() - start) < due) {
      // wait for the next sample time
    }

    int16_t centered = (int16_t)clip.data[i] - 128;
    int16_t duty = (int16_t)128 + (int16_t)(
      ((int32_t)centered * PWM_GAIN_NUMERATOR) /
      PWM_GAIN_DENOMINATOR
    );

    if (duty < 0) {
      duty = 0;
    } else if (duty > 255) {
      duty = 255;
    }

    analogWrite(pwmAudioPin, duty);
  }

  analogWrite(pwmAudioPin, 128); // back to mid-level / silence
}

// Matches the previous Prop-Maker sketch's gain.
constexpr int16_t I2S_GAIN = 25;

void rp2040Service() {
  if (!i2sReady && !pwmReady) {
    return;
  }

  // Set this before examining the queue so an empty queue is reported
  // consistently while the consumer is being serviced.
  core1Busy = true;

  uint8_t id;
  uint16_t generation;

  if (!popSound(id, generation)) {
    core1Busy = false;
    return;
  }

  if (id >= PSOAudio::N_SOUNDS) {
    core1Busy = false;
    return;
  }

  // A stop/new-play event happened after this item was popped but
  // before playback began.
  if (generation != playGeneration) {
    core1Busy = false;
    return;
  }

  const SoundClip &clip = clips[id];

  if (clip.data == nullptr ||
      clip.len == 0 ||
      clip.rate == 0) {

    core1Busy = false;
    return;
  }

  if (pwmReady) {
    // PWM mode: no I2S setup needed.
    soundStartCount[id]++;
    rp2040PwmPlay(clip, generation);
    core1Busy = false;
    return;
  }

  if (!i2sDevice.begin(clip.rate)) {
    core1Busy = false;
    return;
  }

  // Check again after starting I2S. If a new trigger/mode event happened
  // during begin(), do not count or play the stale clip.
  if (generation != playGeneration) {
    i2sDevice.end();
    core1Busy = false;
    return;
  }

  // I2S has genuinely started this sound.
  soundStartCount[id]++;

  for (uint32_t i = 0; i < clip.len; i++) {
    // A fresh event can interrupt a long clip within one sample.
    if (generation != playGeneration) {
      break;
    }

    int16_t centered = (int16_t)clip.data[i] - 128;
    int16_t sample =
      (int16_t)((int32_t)centered * I2S_GAIN);

    i2sDevice.write(sample); // left
    i2sDevice.write(sample); // right
  }

  i2sDevice.end();
  core1Busy = false;
}

#endif // PSOAUDIO_RP2040

// ---------------------------------------------------------------------------
// nRF52840 PWM/Timer1 backend
// ---------------------------------------------------------------------------
#if PSOAUDIO_NRF52840

// TIMER1 runs from 16 MHz / 2^PRESCALER. PRESCALER = 4 gives 1 MHz.
constexpr uint32_t NRF_TIMER_CLOCK_HZ = 1000000UL;

int audioPin = -1;
bool nrfReady = false;

volatile const uint8_t *audioData = nullptr;
volatile uint32_t audioLen = 0;
volatile uint32_t audioPos = 0;
volatile bool audioPlaying = false;

// This preserves the original scaffold's gain calculation:
//
//   centered * 4 / 32 = centered / 8
//
constexpr int32_t NRF_GAIN_NUMERATOR = 16;
constexpr int32_t NRF_GAIN_DENOMINATOR = 32;

// Audio PWM: the nRF52840's NRF_PWM1 peripheral, driven directly.
//
// analogWrite() is deliberately NOT used. On the Arduino nRF52 cores its
// carrier is only ~1 kHz, far too slow for 8 kHz sample playback, and it
// is not safe to call from an ISR. Here the PWM peripheral free-runs at
// 16 MHz / 256 = 62.5 kHz, re-reading one duty value from RAM every
// period. The timer ISR just stores the next duty into that RAM word.
NRF_PWM_Type *const audioPwm = NRF_PWM1;

#ifndef PSOAUDIO_PWM_COUNTERTOP
#define PSOAUDIO_PWM_COUNTERTOP 256   // carrier = 16 MHz / COUNTERTOP - default 256
#endif
constexpr uint16_t NRF_PWM_COUNTERTOP = PSOAUDIO_PWM_COUNTERTOP;

uint16_t pwmDuty[1] = { 0x8000 | 128 };

inline void nrfWriteDuty(uint8_t duty) {
  pwmDuty[0] = (uint16_t)(0x8000 | ((uint32_t)duty * NRF_PWM_COUNTERTOP / 256));
}

void nrfPwmBegin(int pin) {
  audioPwm->ENABLE = 0;
  audioPwm->TASKS_STOP = 1;

  // Make sure the pin is an output before the PWM takes it over.
  pinMode(pin, OUTPUT);

  // Arduino pin number -> absolute nRF GPIO number (Adafruit nRF52 core).
  audioPwm->PSEL.OUT[0] = g_ADigitalPinMap[pin];
  audioPwm->PSEL.OUT[1] = 0xFFFFFFFFUL;
  audioPwm->PSEL.OUT[2] = 0xFFFFFFFFUL;
  audioPwm->PSEL.OUT[3] = 0xFFFFFFFFUL;

  audioPwm->MODE = 0;        // up counter
  audioPwm->PRESCALER = 0;   // PWM clock 16 MHz. (NOT the TIMER1 prescaler.)
  audioPwm->COUNTERTOP = NRF_PWM_COUNTERTOP;   // 16 MHz / 256 = 62.5 kHz
  audioPwm->DECODER = 0;     // common load, refresh-count mode

  pwmDuty[0] = (uint16_t)(0x8000 | (NRF_PWM_COUNTERTOP / 2));

  // Both sequences point at the same one-word buffer. The peripheral only
  // reads RAM while a sequence is running, so loop forever:
  //   SEQ0 -> SEQ1 -> (LOOPSDONE short) -> SEQ0 -> ...
  // Each sequence is one value, re-read every PWM period (REFRESH = 0).
  for (int i = 0; i < 2; i++) {
    audioPwm->SEQ[i].PTR = (uint32_t)(uintptr_t)pwmDuty;
    audioPwm->SEQ[i].CNT = 1;
    audioPwm->SEQ[i].REFRESH = 0;
    audioPwm->SEQ[i].ENDDELAY = 0;
  }

  audioPwm->LOOP = 1;
  audioPwm->SHORTS = (1UL << 2);   // LOOPSDONE -> SEQSTART[0]

  audioPwm->ENABLE = 1;
  audioPwm->TASKS_SEQSTART[0] = 1;
}

// Forward declaration: nrfTimerBegin() uses nrfStop() when the backend
// is initialized more than once.
void nrfStop();

void nrfTimerBegin(int pin) {
  if (nrfReady) {
    nrfStop();
  }

  audioPin = pin;

  nrfPwmBegin(audioPin);

  NVIC_DisableIRQ(TIMER1_IRQn);

  NRF_TIMER1->TASKS_STOP = 1;
  NRF_TIMER1->EVENTS_COMPARE[0] = 0;
  NRF_TIMER1->TASKS_CLEAR = 1;

  NRF_TIMER1->MODE =
    TIMER_MODE_MODE_Timer;

  NRF_TIMER1->BITMODE =
    TIMER_BITMODE_BITMODE_32Bit;

  NRF_TIMER1->PRESCALER =
    4; // 16 MHz / 2^4 = 1 MHz timer tick

  NRF_TIMER1->SHORTS =
    TIMER_SHORTS_COMPARE0_CLEAR_Msk;

  NRF_TIMER1->CC[0] =
    NRF_TIMER_CLOCK_HZ / 8000UL;

  NRF_TIMER1->INTENSET =
    TIMER_INTENSET_COMPARE0_Msk;

  audioData = nullptr;
  audioLen = 0;
  audioPos = 0;
  audioPlaying = false;

  nrfReady = true;

  NVIC_SetPriority(TIMER1_IRQn, 1);
  NVIC_ClearPendingIRQ(TIMER1_IRQn);
  NVIC_EnableIRQ(TIMER1_IRQn);

  //NRF_TIMER1->TASKS_START = 1;
}

void nrfStop() {
  if (!nrfReady) {
    return;
  }

  noInterrupts();

  audioPlaying = false;
  audioData = nullptr;
  audioLen = 0;
  audioPos = 0;

  interrupts();
  
   NRF_TIMER1->TASKS_STOP = 1;

  nrfWriteDuty(128);
}

bool nrfStart(uint8_t id) {
  if (!nrfReady || id >= PSOAudio::N_SOUNDS) {
    return false;
  }

  const SoundClip &clip = clips[id];

  if (clip.data == nullptr ||
      clip.len == 0 ||
      clip.rate == 0) {

    return false;
  }

  if (clip.rate > NRF_TIMER_CLOCK_HZ) {
    return false;
  }

  // Round to the closest available number of 1 MHz timer ticks.
  uint32_t timerPeriod =
    (NRF_TIMER_CLOCK_HZ + (clip.rate / 2UL)) / clip.rate;

  if (timerPeriod == 0) {
    timerPeriod = 1;
  }

  /*
   * Configure the timer and clip before enabling audioPlaying.
   *
   * This avoids the race in the original sketch, where audioPlaying
   * was set before the timer sample rate was changed.
   */
  noInterrupts();

  NRF_TIMER1->CC[0] = timerPeriod;

  audioData = clip.data;
  audioLen = clip.len;
  audioPos = 0;
  audioPlaying = true;

   NRF_TIMER1->EVENTS_COMPARE[0] = 0;
   NRF_TIMER1->TASKS_CLEAR = 1;
   NRF_TIMER1->TASKS_START = 1;

  interrupts();

  nrfWriteDuty(128);

  // Count only after timer-backed playback has started.
  soundStartCount[id]++;

  return true;
}

void nrfService() {
  if (audioPlaying) {
    return;
  }

  uint8_t id;
  uint16_t generation;

  if (!popSound(id, generation)) {
    return;
  }

  if (id >= PSOAudio::N_SOUNDS) {
    return;
  }

  // A newer event happened after this item was popped.
  if (generation != playGeneration) {
    return;
  }

  nrfStart(id);
}

void nrfTimerIsr() {
  if (NRF_TIMER1->EVENTS_COMPARE[0] == 0) {
    return;
  }

  NRF_TIMER1->EVENTS_COMPARE[0] = 0;

  if (!audioPlaying) {
    return;
  }

  if (audioPos < audioLen) {
    uint8_t raw = audioData[audioPos];
    audioPos++;

    int16_t centered = (int16_t)raw - 128;

    int16_t scaled = (int16_t)(
      ((int32_t)centered * NRF_GAIN_NUMERATOR) /
      NRF_GAIN_DENOMINATOR
    );

    int16_t duty = (int16_t)128 + scaled;

    if (duty < 0) {
      duty = 0;
    } else if (duty > 255) {
      duty = 255;
    }

    // Just a RAM store; the PWM peripheral picks it up next period.
    nrfWriteDuty((uint8_t)duty);
  } else {
    audioPlaying = false;
	NRF_TIMER1->TASKS_STOP = 1;
    nrfWriteDuty(128);
  }
}

#endif // PSOAUDIO_NRF52840

} // anonymous namespace

// The interrupt entry point must have C linkage.
#if PSOAUDIO_NRF52840
extern "C" void TIMER1_IRQHandler(void) {
  nrfTimerIsr();
}
#endif

namespace PSOAudio {

bool begin(const Pins &pins) {
#if PSOAUDIO_RP2040

  // I2S pins given: I2S output (Adafruit Feather RP2040 Prop-Maker).
  if (pins.bclkPin >= 0 && pins.dataPin >= 0) {
    i2sDevice.setBCLK(pins.bclkPin);
    i2sDevice.setDATA(pins.dataPin);
    i2sDevice.setBitsPerSample(16);

    pwmReady = false;
    i2sReady = true;
    return true;
  }

  // Only a PWM pin given: PWM output (e.g. Photon Drop on XIAO RP2040).
  if (pins.pwmPin >= 0) {
    pwmAudioPin = pins.pwmPin;

    pinMode(pwmAudioPin, OUTPUT);
    analogWriteFreq(PWM_CARRIER_HZ);
    analogWriteRange(PWM_RANGE);
    analogWrite(pwmAudioPin, 128);

    i2sReady = false;
    pwmReady = true;
    return true;
  }

  return false;

#elif PSOAUDIO_NRF52840

  if (pins.pwmPin < 0) {
    return false;
  }

  nrfTimerBegin(pins.pwmPin);
  return nrfReady;

#else

  (void)pins;
  return false;

#endif
}

void queueSound(SoundId id) {
  uint8_t rawId = (uint8_t)id;

  if (rawId >= N_SOUNDS) {
    return;
  }

  lockQueue();

  if (queueCount < QUEUE_LENGTH) {
    soundQueue[queueHead] = rawId;
    queueHead = (queueHead + 1) % QUEUE_LENGTH;
    queueCount++;
  }

  // If the queue is full, the new sound is dropped rather than
  // blocking gameplay.

  unlockQueue();
}

void interruptAndClearQueue() {
  lockQueue();

  playGeneration++;
  queueHead = queueTail;
  queueCount = 0;

  unlockQueue();

#if PSOAUDIO_NRF52840
  nrfStop();
#endif
}

void startClip(SoundId id) {
  if ((uint8_t)id >= N_SOUNDS) {
    return;
  }

  interruptAndClearQueue();
  queueSound(id);

#if PSOAUDIO_NRF52840
  // nRF52840 has no second audio loop, so start it immediately.
  serviceAudioQueue();
#endif
}

void serviceAudioQueue() {
#if PSOAUDIO_RP2040
  rp2040Service();
#elif PSOAUDIO_NRF52840
  nrfService();
#endif
}

bool isPlaying() {
  if (queueCount > 0) {
    return true;
  }

#if PSOAUDIO_RP2040
  return core1Busy;
#elif PSOAUDIO_NRF52840
  return audioPlaying;
#else
  return false;
#endif
}

uint16_t startCount(SoundId id) {
  uint8_t rawId = (uint8_t)id;

  if (rawId >= N_SOUNDS) {
    return 0;
  }

  return soundStartCount[rawId];
}

} // namespace PSOAudio
