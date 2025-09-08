#include <Adafruit_NeoPixel.h>

/*
  5 "flames" of 3 pixels each.
  Each flame can have a brightness of 0..(rez_range-1). We scale across 3 LEDs.
*/

#define PIN 2                 // NeoPixel pin
#define NUMBER_OF_FLAMES 5    // 5 triplets -> 15 pixels (e.g., on a 16-ring)
#define FLAME_WIDTH 3         // LEDs per flame
#define FLICKER_CHANCE 3      // Higher -> more rekindling during decay

// Strip: total pixels = flames * width
Adafruit_NeoPixel strip(NUMBER_OF_FLAMES * FLAME_WIDTH, PIN, NEO_GRB + NEO_KHZ800);

// Scaling constants
constexpr uint16_t REZ_RANGE = 256 * 3;   // 768, spreads 0..255 across 3 subpixels
constexpr uint16_t S = REZ_RANGE;         // alias used in palette

// Debug toggle (not used, but kept for convenience)
#define D_ false

struct flame_element {
  int brightness;
  int step;
  int max_brightness;
  long rgb[3];   // base color per flame (scaled from palette)
  uint8_t state; // 0 reset, 1 rising, 2 falling
} flames[NUMBER_OF_FLAMES];

int new_brightness = 0;
// Reusable temporaries
uint32_t tmpRGB[3];
uint8_t scaleD_rgb[3];

const uint16_t flamecolors[22][3] = {
  // Lots of warm reds through orange, plus a couple of “cool” surprises near the end
  { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 },
  { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 }, { S, 0, 0 },

  { S, (uint16_t)(S * 2 / 5), 0 }, { S, (uint16_t)(S * 2 / 5), 0 },
  { S, (uint16_t)(S * 2 / 5), 0 }, { S, (uint16_t)(S * 2 / 5), 0 },

  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },
  { S, (uint16_t)(S * 3 / 10), 0 }, { S, (uint16_t)(S * 3 / 10), 0 },

  { S, (uint16_t)(S * 3 / 10), 0 },                  // filled missing blue with 0
  { S, (uint16_t)(S * 3 / 10), S },                  // “white-ish” hot
  { 0, (uint16_t)(S * 1 / 5), S },                   // occasional blue flame
  { S, (uint16_t)(S * 3 / 10), (uint16_t)(S / 2) }   // reddish with some blue
};

void InitFlames();
void CreateNewFlame(uint8_t flame_num);
void UpdateFlameColor(uint8_t flame_num, int new_brightness);
int  GetStepSize();
int  GetMaxBrightness();

void setup() {
  Serial.begin(9600);
  if (D_) Serial.println(F("STARTUP"));

  strip.begin();
  strip.clear();
  strip.show();

  // Seed RNG (A4 is fine if floating; on some boards it’s SDA, still OK) -- A2 nRF52840 Ali express !!!
  randomSeed(analogRead(A2));

  InitFlames();
}

void loop() {
  for (uint8_t flame_count = 0; flame_count < NUMBER_OF_FLAMES; flame_count++) {
    switch (flames[flame_count].state) {
      case 0: // reset -> spawn new flame
        CreateNewFlame(flame_count);
        break;

      case 1: { // increasing
        new_brightness = flames[flame_count].brightness + flames[flame_count].step;
        if (new_brightness > flames[flame_count].max_brightness) {
          UpdateFlameColor(flame_count, flames[flame_count].max_brightness);
          flames[flame_count].brightness = flames[flame_count].max_brightness;
          flames[flame_count].step = GetStepSize(); // new speed for decay
          flames[flame_count].state = 2;
        } else {
          UpdateFlameColor(flame_count, new_brightness);
          flames[flame_count].brightness = new_brightness;
        }
      } break;

      case 2: { // decreasing
        new_brightness = flames[flame_count].brightness - flames[flame_count].step;

        // Rekindle chance is higher when dim (random(n) with small n tends toward 0)
        long n = new_brightness;
        if (n < 1) n = 1; // avoid random(0)
        if (random(n) < FLICKER_CHANCE) {
          // Rekindle
          flames[flame_count].state = 1;
          flames[flame_count].brightness = max(GetMaxBrightness(), flames[flame_count].brightness);
          flames[flame_count].step = GetStepSize();
        } else {
          if (new_brightness < 1) {
            flames[flame_count].state = 0; // extinguished -> reset
            flames[flame_count].brightness = 0;
            UpdateFlameColor(flame_count, 0);
          } else {
            UpdateFlameColor(flame_count, new_brightness);
            flames[flame_count].brightness = new_brightness;
          }
        }
      } break;
    }
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
  // Cap to this flame’s max
  if (new_brightness > flames[flame_num].max_brightness) {
    new_brightness = flames[flame_num].max_brightness;
  } else if (new_brightness < 0) {
    new_brightness = 0;
  }

  // Scale each channel from base color by brightness / REZ_RANGE
  for (uint8_t ch = 0; ch < 3; ch++) {
    uint32_t base = (uint32_t)flames[flame_num].rgb[ch];    // up to ~768
    uint32_t v = (base * (uint32_t)new_brightness) / (uint32_t)REZ_RANGE;
    tmpRGB[ch] = v;                                          // 0..255-ish spread across 3
  }

  // Distribute channel value across the 3 LEDs in the flame
  for (uint8_t sub = 0; sub < FLAME_WIDTH; sub++) {
    for (uint8_t ch = 0; ch < 3; ch++) {
      uint8_t q = tmpRGB[ch] / 3;
      uint8_t r = tmpRGB[ch] % 3;
      scaleD_rgb[ch] = q + (sub < r ? 1 : 0);
    }
    uint32_t c = strip.Color(scaleD_rgb[0], scaleD_rgb[1], scaleD_rgb[2]);
    strip.setPixelColor(flame_num * FLAME_WIDTH + sub, c);
  }
}

void CreateNewFlame(uint8_t flame_num) {
  flames[flame_num].step = GetStepSize();
  flames[flame_num].max_brightness = GetMaxBrightness();
  flames[flame_num].brightness = 0;
  flames[flame_num].state = 1;

  uint8_t color_index = (uint8_t)random(0, 22); // 0..21
  for (uint8_t i = 0; i < 3; i++) {
    flames[flame_num].rgb[i] = flamecolors[color_index][i];
  }
}

int GetStepSize() {
  // Tune for rise/decay speed variability
  return (int)random(1, 71); // 1..70
}

int GetMaxBrightness() {
  // Brighter flat distribution: REZ_RANGE/2 .. REZ_RANGE-1
  return (int)(random(REZ_RANGE / 2) + REZ_RANGE / 2);
}
