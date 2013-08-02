/*
  G35: An Arduino library for GE Color Effects G-35 holiday lights.

  Copyright © 2011 The G35 Authors. Use, modification, and distribution are
  subject to the BSD license as described in the accompanying LICENSE file.

  Original version by Paul Martis (http://www.digitalmisery.com). See
  README for complete attributions.
*/

#include <WProgram.h>
#include <G35.h>
#include "digitalWriteFast.h"

G35::G35(int pin, int light_count) {
  pinModeFast(19, OUTPUT);
  _pin = pin;
  _light_count = light_count;
}

// The timings below have been hand-tuned. Adjust if necessary.
void G35::begin() {
  digitalWriteFast(19, HIGH);
  delayMicroseconds(10);  // Output should be ~ 10uS long
  digitalWriteFast(19, LOW);
}

void G35::one() {
  digitalWriteFast(19, LOW);
  delayMicroseconds(20);  // Output should be ~ 20uS long
  digitalWriteFast(19, HIGH);
  delayMicroseconds(10);   // Output should be ~ 10uS long
  digitalWriteFast(19, LOW);
}

void G35::zero() {
  digitalWriteFast(19, LOW);
  delayMicroseconds(10);  // Output should be ~ 10uS long
  digitalWriteFast(19, HIGH);
  delayMicroseconds(20);  // Output should be ~ 20uS long
  digitalWriteFast(19, LOW);
}

void G35::end() {
  digitalWriteFast(19, LOW);
  delayMicroseconds(30);  // Quiet time should be ~ 30us long
}

void G35::set_color(uint8_t led, uint8_t intensity, color_t color) {
  uint8_t i;
  begin();
  for (i = 6; i; i--, (led <<= 1)) {
    if (led & (1 << 5)) {
      one();
    } else {
      zero();
    }
  }
  for (i = 8; i; i--, (intensity <<= 1)) {
    if (intensity & (1 << 7)) {
      one();
    } else {
      zero();
    }
  }

  for (i=12; i; i--, (color <<= 1)) {
    if (color & (1 << 11)) {
      one();
    } else {
      zero();
    }
  }
  end();
}

bool G35::set_color_if_in_range(uint8_t position, uint8_t intensity,
                color_t color) {
  if (position >= _light_count) {
    return false;
  }
  set_color(position, intensity, color);
  return true;
}


// Returns 12-bit color from red, green, and blue components
color_t G35::color(uint8_t r, uint8_t g, uint8_t b) {
  return COLOR(r, g, b);
}

color_t G35::color_hue(uint8_t h) {
  switch (h >> 4) {
  case 0:     h -= 0; return color(h, CHANNEL_MAX, 0);
  case 1:     h -= 16; return color(CHANNEL_MAX, (CHANNEL_MAX - h), 0);
  case 2:     h -= 32; return color(CHANNEL_MAX, 0, h);
  case 3:     h -= 48; return color((CHANNEL_MAX - h), 0, CHANNEL_MAX);
  case 4:     h -= 64; return color(0, h, CHANNEL_MAX);
  case 5:     h -= 80; return color(0, CHANNEL_MAX, (CHANNEL_MAX - h));
  }
}

void G35::fill_color(uint8_t begin, uint8_t count,
                     uint8_t intensity, color_t color) {
  while (count--) {
    set_color(begin++, intensity, color);
  }
}

void G35::fill_random_max(uint8_t begin, uint8_t count, uint8_t intensity) {
  while (count--) {
    set_color(begin++, intensity, max_color(rand()));
  }
}

void G35::fill_sequence(uint8_t begin, uint8_t count,
            uint16_t sequence, uint8_t span_size,
            uint8_t intensity,
            color_t (*sequence_func)(uint16_t sequence)) {
  while (count--) {
    set_color(begin + count, intensity,
          sequence_func(sequence++ / span_size));
  }
}

void G35::fill_sequence(uint16_t sequence, uint8_t span_size,
            uint8_t intensity,
color_t (*sequence_func)(uint16_t sequence)) {
  fill_sequence(0, _light_count, sequence, span_size, intensity, sequence_func);
}

void G35::fill_sequence(uint8_t begin, uint8_t count,
                        uint16_t sequence, uint8_t span_size,
                        bool (*sequence_func)(uint16_t sequence, color_t& color,
                                              uint8_t& intensity)) {
  while (count--) {
    color_t color;
    uint8_t intensity;
    sequence_func(sequence++ / span_size, color, intensity);
    set_color(begin + count, intensity, color);
  }
}

void G35::enumerate(bool reverse) {
  uint8_t count = _light_count;
  uint8_t bulb = reverse ? _light_count - 1 : 0;
  int8_t delta = reverse ? -1 : 1;
  while (count--) {
    set_color(bulb, MAX_INTENSITY, COLOR_BLACK);
    bulb += delta;
  }
}

uint16_t G35::get_light_count() {
  return _light_count;
}

void G35::enumerate_forward() {
  enumerate(false);
}

void G35::enumerate_reverse() {
  enumerate(true);
}

void G35::test_patterns() {
  fill_color(0, _light_count, 0, rainbow_color(RB_FIRST));
  fade_in(1);
  for (uint8_t i = RB_FIRST; i <= RB_LAST; ++i) {
    delay(50);
    fill_color(0, _light_count, MAX_INTENSITY, rainbow_color(i));
  }
  fade_out(1);
  fill_color(0, _light_count, MAX_INTENSITY, COLOR_BLACK);
  delay(500);
}

void G35::steady_multi() {
   for (uint8_t i=0; i<_light_count; i=i+5){
     set_color(i, 0, COLOR_RED);
     set_color(i+1, 0, COLOR_GREEN);
     set_color(i+2, 0, COLOR_ORANGE);
     set_color(i+3, 0, COLOR_BLUE);
     set_color(i+4, 0, COLOR_YELLOW);
   }        
   fade_in(10);
}

color_t G35::rainbow_color(uint16_t color) {
  if (color >= RB_COUNT) {
    color = color % RB_COUNT;
  }
  switch (color) {
  case RB_RED:
    return COLOR_RED;
  case RB_ORANGE:
    return COLOR_ORANGE;
  case RB_YELLOW:
    return COLOR_YELLOW;
  case RB_GREEN:
    return COLOR_GREEN;
  case RB_BLUE:
    return COLOR_BLUE;
  case RB_INDIGO:
    return COLOR_INDIGO;
  case RB_VIOLET:
    return COLOR_VIOLET;
  }
}

color_t G35::max_color(uint16_t color) {
  if (color >= 6) {
    color = color % 6;
  }
  switch (color) {
  case 0:
    return COLOR_RED;
  case 1:
    return COLOR_GREEN;
  case 2:
    return COLOR_BLUE;
  case 3:
    return COLOR_CYAN;
  case 4:
    return COLOR_MAGENTA;
  case 5:
    return COLOR_YELLOW;
  }
}

void G35::fade_in(uint8_t delay_msec) {
  for (int i = 0; i <= MAX_INTENSITY; ++i) {
    set_color(63, i, COLOR_BLACK);
    delay(delay_msec);
  }
}

void G35::fade_out(uint8_t delay_msec) {
  for (int i = MAX_INTENSITY; i >= 0; --i) {
    set_color(63, i, COLOR_BLACK);
    delay(delay_msec);
  }
}
