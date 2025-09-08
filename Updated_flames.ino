#include <Adafruit_NeoPixel.h>

// -------------------- Config --------------------
#define PIN 2
#define TOTAL_PIXELS 11

// Make this anything >=1. Common choices: 2..5
#define FLAME_WIDTH 3

// How many flames (triplets of width FLAME_WIDTH)
#define NUMBER_OF_FLAMES 3

#define FLICKER_CHANCE 3

// ------------------------------------------------
Adafruit_NeoPixel strip(TOTAL_PIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Scale so brightness maps cleanly across FLAME_WIDTH subpixels
constexpr uint16_t REZ_RANGE = 256 * FLAME_WIDTH;
constexpr uint16_t S = REZ_RANGE;

struct flame_element {
  int brightness;
  int step;
  int max_brightness;
  long rgb[3];
  uint8_t state; // 0 reset, 1 rising, 2 falling
} flames[NUMBER_OF_FLAMES];

int new_brightness = 0;
uint32_t tmpRGB[3];
uint8_t scaleD_rgb[3];

// Build palette in terms of S so it auto-scales with FLAME_WIDTH
const uint16_t flamecolors[][3] = {
  { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 },
  { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 },
  { S, (uint16_t)(S * 2 / 5), 0 }, { S, (uint16_t)(S * 2 / 5), 0 },
  { S, (uint16_t)(S * 2 / 5), 0 }, { S, (uint16_t)(S * 2 / 5), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), S },       // hot white-ish
  { 0, (uint16_t)(S * 1 / 5), S },        // blue flame
  { S, (uint16_t)(S * 3 / 10), S / 2 }    // reddish with blue
};
constexpr uint8_t PALETTE_SIZE = sizeof(flamecolors) / sizeof(flamecolors[0]);

void InitFlames();
void CreateNewFlame(uint8_t flame_num);
void UpdateFlameColor(uint8_t flame_num, int new_brightness);
int  GetStepSize();
int  GetMaxBrightness();

void setup() {
  Serial.begin(9600);
  strip.begin();
  strip.clear();
  strip.show();
  randomSeed(analogRead(A4));   // any floating analog pin works

  InitFlames();
}

void loop() {
  for (uint8_t f = 0; f < NUMBER_OF_FLAMES; f++) {
    switch (flames[f].state) {
      case 0:
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
          flames[f].state = 1; // rekindle
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

  // Optional: handle any pixels beyond flames as embers (or comment out to keep off)
  const int used = NUMBER_OF_FLAMES * FLAME_WIDTH;
  for (int i = used; i < TOTAL_PIXELS; i++) {
    uint8_t ember = random(4); // 0..3
    strip.setPixelColor(i, strip.Color(4 + ember, 1, 0));
  }

  strip.show();
  delay(22);
}

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
    uint32_t base = (uint32_t)flames[flame_num].rgb[ch]; // up to ~S
    uint32_t v = (base * (uint32_t)new_brightness) / (uint32_t)REZ_RANGE;
    tmpRGB[ch] = v; // 0..~255, to be distributed
  }

  // Distribute across FLAME_WIDTH LEDs using quotient/remainder
  for (uint8_t sub = 0; sub < FLAME_WIDTH; sub++) {
    for (uint8_t ch = 0; ch < 3; ch++) {
      uint8_t q = tmpRGB[ch] / FLAME_WIDTH;
      uint8_t r = tmpRGB[ch] % FLAME_WIDTH;
      scaleD_rgb[ch] = q + (sub < r ? 1 : 0);
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

  uint8_t color_index = (uint8_t)random(0, PALETTE_SIZE);
  for (uint8_t i = 0; i < 3; i++) {
    flames[flame_num].rgb[i] = flamecolors[color_index][i];
  }
}

int GetStepSize() {
  return (int)random(1, 71); // 1..70 (tweak for speed)
}

int GetMaxBrightness() {
  // Flat bright distribution
  return (int)(random(REZ_RANGE / 2) + REZ_RANGE / 2); // [REZ_RANGE/2 .. REZ_RANGE-1]
}
