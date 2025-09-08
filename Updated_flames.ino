#include <Adafruit_NeoPixel.h>

/* -------------------------------------------------------
   Flame Ring — variable-width flames with realistic colors
   -------------------------------------------------------
   Defaults match your current setup:
   - TOTAL_PIXELS = 11
   - FLAME_WIDTH  = 3
   - NUMBER_OF_FLAMES = 3  (9 used + 2 embers)
   You can change these three defines at the top.
*/

#define PIN 8
#define TOTAL_PIXELS 11
#define FLAME_WIDTH 3
#define NUMBER_OF_FLAMES 3

#define FLICKER_CHANCE 3     // higher => more rekindling during decay
#define USE_WEIGHTED_PALETTE 1
#define CENTER_BIAS 1        // center subpixel gets priority in brightness
#define BLUE_POP_WEIGHT 2    // 0..100 (% of times blue entries are allowed)

// -------------------------------------------------------
Adafruit_NeoPixel strip(TOTAL_PIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Scale so brightness maps cleanly across FLAME_WIDTH subpixels
constexpr uint16_t REZ_RANGE = 256 * FLAME_WIDTH;
constexpr uint16_t S = REZ_RANGE;

// Per-flame state
struct flame_element {
  int brightness;
  int step;
  int max_brightness;
  uint16_t rgb[3]; // base color scaled in 0..S
  uint8_t state;   // 0 reset, 1 rising, 2 falling
} flames[NUMBER_OF_FLAMES];

int new_brightness = 0;
uint16_t tmpRGB[3];      // per-channel scaled value before distribution
uint8_t scaleD_rgb[3];   // final per-subpixel RGB to set

// Precomputed order for distributing the remainder to subpixels
// and the inverse: the "rank" of each subpixel in that order.
uint8_t remainderOrder[FLAME_WIDTH];
uint8_t rankOfSub[FLAME_WIDTH];

// ---------------- Palette (auto-scales with S) ----------------
const uint16_t flamecolors[][3] = {
  // Warm core tones (heavily weighted)
  { S,        0,           0 },       // deep red
  { S,        S*1/10,      0 },       // red-orange
  { S,        S*2/10,      0 },       // orange
  { S,        S*3/10,      0 },       // amber
  { S,        S*4/10,      0 },       // bright amber
  { S,        S*5/10,      S*1/20 },  // near-yellow with whisper of blue
  // Hot/white tips (moderate)
  { S,        S*3/10,      S*2/10 },  // hot white-ish
  { S,        S*2/10,      S*3/10 },  // hotter white-ish
  // Rare blue pops
  { S*1/10,   S*2/10,      S        }, // blue flame
  { 0,        S*2/10,      S        }  // pure blue w/ hint of green
};
constexpr uint8_t PALETTE_SIZE = sizeof(flamecolors) / sizeof(flamecolors[0]);

// Heavier on warm colors, lighter on blue/white
const uint8_t paletteWeights[PALETTE_SIZE] = {
  18, 16, 16, 14, 10, 8,   8, 6,   2, 2
};

// ------------------ Helpers / Prototypes ------------------
void InitFlames();
void CreateNewFlame(uint8_t flame_num);
void UpdateFlameColor(uint8_t flame_num, int new_brightness);
int  GetStepSize();
int  GetMaxBrightness();
void buildRemainderOrder();
uint8_t getWeightedPaletteIndex();
static inline uint16_t clampS(long v);
void applyTempJitter(uint16_t base[3]);

void setup() {
  Serial.begin(9600);
  strip.begin();
  strip.clear();
  strip.show();

  // Seed RNG — use an unconnected floating analog pin (A2 is good)
  randomSeed(analogRead(A2));

  buildRemainderOrder();
  InitFlames();
}

void loop() {
  for (uint8_t f = 0; f < NUMBER_OF_FLAMES; f++) {
    switch (flames[f].state) {
      case 0: // reset -> spawn new flame
        CreateNewFlame(f);
        break;

      case 1: { // rising
        new_brightness = flames[f].brightness + flames[f].step;
        if (new_brightness > flames[f].max_brightness) {
          UpdateFlameColor(f, flames[f].max_brightness);
          flames[f].brightness = flames[f].max_brightness;
          flames[f].step = GetStepSize();
          flames[f].state = 2;
        } else {
          UpdateFlameColor(f, new_brightness);
          flames[f].brightness = new_brightness;
        }
      } break;

      case 2: { // falling
        new_brightness = flames[f].brightness - flames[f].step;
        long n = new_brightness; if (n < 1) n = 1;
        if (random(n) < FLICKER_CHANCE) {
          // Rekindle
          flames[f].state = 1;
          flames[f].brightness = max(GetMaxBrightness(), flames[f].brightness);
          flames[f].step = GetStepSize();
        } else {
          if (new_brightness < 1) {
            flames[f].state = 0;
            flames[f].brightness = 0;
            UpdateFlameColor(f, 0);
          } else {
            UpdateFlameColor(f, new_brightness);
            flames[f].brightness = new_brightness;
          }
        }
      } break;
    }
  }

  // Optional: embers for leftover pixels (those not covered by flames)
  const int used = NUMBER_OF_FLAMES * FLAME_WIDTH;
  for (int i = used; i < TOTAL_PIXELS; i++) {
    uint8_t ember = random(4); // 0..3
    strip.setPixelColor(i, strip.Color(4 + ember, 1, 0)); // very dim warm
  }

  strip.show();
  delay(22);
}

// -----------------------------------------------------------

void InitFlames() {
  for (uint8_t i = 0; i < NUMBER_OF_FLAMES; i++) {
    flames[i].state = 0;
    flames[i].brightness = 0;
    flames[i].step = 0;
    flames[i].max_brightness = REZ_RANGE / 2;
    flames[i].rgb[0] = flames[i].rgb[1] = flames[i].rgb[2] = 0;
  }
}

void UpdateFlameColor(uint8_t flame_num, int new_brightness) {
  if (new_brightness > flames[flame_num].max_brightness) new_brightness = flames[flame_num].max_brightness;
  if (new_brightness < 0) new_brightness = 0;

  // Scale each color channel by brightness / REZ_RANGE
  for (uint8_t ch = 0; ch < 3; ch++) {
    uint32_t base = (uint32_t)flames[flame_num].rgb[ch]; // 0..S
    uint32_t v = (base * (uint32_t)new_brightness) / (uint32_t)REZ_RANGE;
    if (v > 255) v = 255; // safety clamp
    tmpRGB[ch] = (uint16_t)v; // 0..255-ish (to be distributed)
  }

  // Distribute per subpixel with center-bias order
  for (uint8_t sub = 0; sub < FLAME_WIDTH; sub++) {
    for (uint8_t ch = 0; ch < 3; ch++) {
      uint8_t q = tmpRGB[ch] / FLAME_WIDTH;
      uint8_t r = tmpRGB[ch] % FLAME_WIDTH;
      #if CENTER_BIAS
        // Give +1 to the first 'r' positions in remainderOrder
        uint8_t extra = (rankOfSub[sub] < r) ? 1 : 0;
        scaleD_rgb[ch] = q + extra;
      #else
        scaleD_rgb[ch] = q + (sub < r ? 1 : 0);
      #endif
    }

    const int idx = flame_num * FLAME_WIDTH + sub;
    if (idx < TOTAL_PIXELS) {
      strip.setPixelColor(idx, strip.Color(scaleD_rgb[0], scaleD_rgb[1], scaleD_rgb[2]));
    }
  }
}

void CreateNewFlame(uint8_t flame_num) {
  flames[flame_num].step = GetStepSize();
  flames[flame_num].max_brightness = GetMaxBrightness();
  flames[flame_num].brightness = 0;
  flames[flame_num].state = 1;

  uint8_t idx = getWeightedPaletteIndex();
  uint16_t base[3] = {
    flamecolors[idx][0],
    flamecolors[idx][1],
    flamecolors[idx][2]
  };

  applyTempJitter(base);

  flames[flame_num].rgb[0] = base[0];
  flames[flame_num].rgb[1] = base[1];
  flames[flame_num].rgb[2] = base[2];
}

int GetStepSize() {
  // 1..70 (tweak upper bound for faster/slower dynamics)
  return (int)random(1, 71);
}

int GetMaxBrightness() {
  // Flat bright distribution: [REZ_RANGE/2 .. REZ_RANGE-1]
  return (int)(random(REZ_RANGE / 2) + REZ_RANGE / 2);
}

// --------------------- Distribution order ---------------------
void buildRemainderOrder() {
  // Build an order that prioritizes the center, then neighbors.
  // Works for odd or even FLAME_WIDTH.
  // Also build rankOfSub[sub] = position of 'sub' in that order.
  uint8_t pos = 0;

  if (FLAME_WIDTH == 1) {
    remainderOrder[0] = 0;
    rankOfSub[0] = 0;
    return;
  }

  // For even widths, prefer the left-of-center first, then right-of-center
  uint8_t centerL = (FLAME_WIDTH - 1) / 2;
  uint8_t centerR = FLAME_WIDTH / 2;

  // Seed with the “centers”
  remainderOrder[pos++] = centerL;
  if (centerR != centerL) remainderOrder[pos++] = centerR;

  // Then expand outward alternately left and right
  for (uint8_t off = 1; pos < FLAME_WIDTH; off++) {
    int left  = (int)centerL - (int)off;
    int right = (int)centerR + (int)off;
    if (left >= 0 && pos < FLAME_WIDTH)  remainderOrder[pos++] = (uint8_t)left;
    if (right < FLAME_WIDTH && pos < FLAME_WIDTH) remainderOrder[pos++] = (uint8_t)right;
  }

  // Build inverse map
  for (uint8_t i = 0; i < FLAME_WIDTH; i++) {
    // find i in remainderOrder
    for (uint8_t r = 0; r < FLAME_WIDTH; r++) {
      if (remainderOrder[r] == i) {
        rankOfSub[i] = r;
        break;
      }
    }
  }
}

// ---------------- Palette selection & jitter ------------------
uint8_t getWeightedPaletteIndex() {
#if USE_WEIGHTED_PALETTE
  // Optionally clamp out the last two (blue-ish) entries most of the time
  bool allowBlue = (random(100) < BLUE_POP_WEIGHT);
  uint16_t total = 0;
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    if (!allowBlue && (i >= PALETTE_SIZE - 2)) continue;
    total += paletteWeights[i];
  }

  uint16_t r = (total > 0) ? random(total) : 0;
  uint16_t acc = 0;
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    if (!allowBlue && (i >= PALETTE_SIZE - 2)) continue;
    acc += paletteWeights[i];
    if (r < acc) return i;
  }
  return (uint8_t)(PALETTE_SIZE - 1);
#else
  return (uint8_t)random(0, PALETTE_SIZE);
#endif
}

static inline uint16_t clampS(long v) {
  if (v < 0) return 0;
  if (v > S) return S;
  return (uint16_t)v;
}

void applyTempJitter(uint16_t base[3]) {
  // Small +/- tweaks w/out floats; keeps within 0..S
  int16_t jR = (int16_t)random(- (int)(S/40), (int)(S/40) + 1); // ~±2.5%
  int16_t jG = (int16_t)random(- (int)(S/50), (int)(S/30) + 1); // slight warm bias
  int16_t jB = (int16_t)random(- (int)(S/40), (int)(S/60) + 1); // slight cool damp

  base[0] = clampS((long)base[0] + jR);
  base[1] = clampS((long)base[1] + jG);
  base[2] = clampS((long)base[2] + jB);
}
