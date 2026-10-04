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

// Matches the previous Prop-Maker sketch's gain.
constexpr int16_t I2S_GAIN = 25;

void rp2040Service() {
  if (!i2sReady) {
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

// The nRF52840 TIMER peripheral runs from a 1 MHz timer clock when its
// prescaler is zero.
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
constexpr int32_t NRF_GAIN_NUMERATOR = 4;
constexpr int32_t NRF_GAIN_DENOMINATOR = 32;

inline void nrfWriteDuty(uint8_t duty) {
  analogWrite(audioPin, duty);
}

// Forward declaration: nrfTimerBegin() uses nrfStop() when the backend
// is initialized more than once.
void nrfStop();

void nrfTimerBegin(int pin) {
  if (nrfReady) {
    nrfStop();
  }

  audioPin = pin;

  pinMode(audioPin, OUTPUT);
  analogWrite(audioPin, 128);

  NVIC_DisableIRQ(TIMER1_IRQn);

  NRF_TIMER1->TASKS_STOP = 1;
  NRF_TIMER1->EVENTS_COMPARE[0] = 0;
  NRF_TIMER1->TASKS_CLEAR = 1;

  NRF_TIMER1->MODE =
    TIMER_MODE_MODE_Timer;

  NRF_TIMER1->BITMODE =
    TIMER_BITMODE_BITMODE_32Bit;

  NRF_TIMER1->PRESCALER =
    0; // 1 MHz timer tick

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

  NRF_TIMER1->TASKS_START = 1;
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

    // Compatibility implementation retained from the original sketch.
    // analogWrite() inside an ISR can still cause timing glitches on
    // real hardware.
    nrfWriteDuty((uint8_t)duty);
  } else {
    audioPlaying = false;
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

  if (pins.bclkPin < 0 || pins.dataPin < 0) {
    return false;
  }

  i2sDevice.setBCLK(pins.bclkPin);
  i2sDevice.setDATA(pins.dataPin);
  i2sDevice.setBitsPerSample(16);

  i2sReady = true;
  return true;

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
