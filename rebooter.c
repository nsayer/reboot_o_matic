/*

 Reboot-o-matic
 Copyright 2019 Nicholas W. Sayer
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warran of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdlib.h>
#include <stdint.h>

// Where is the hardware?
#define IN_PIN_MASK _BV(4)
#define OUT_PIN_MASK _BV(3)

// We want a 1 ms basic tick, but we're dividing 1 MHz by 64 for the timer
// clock. So one tick is 15 5/8 timer clocks. So we count 16 5 times
// and 15 3 times and repeat
#define TICK_BASE (15)
#define TICK_NUMERATOR (5)
#define TICK_DENOMINATOR (8)

// This is how long the input has to stay at a particular
// state for a change to be recognized.
// about a second
#define DEBOUNCE_TICKS (1000UL)

// This is how long the power is turned off when the input is asserted
// about 10 seconds
#define RESET_TICKS (10000UL)

// This is how long subsequente reset pulses will be ignored after one is
// recognized and the power is cycled.
// about an hour
#define RESET_HOLDOFF (3600UL * 1000)

// Tick counter. This is not allowed to equal zero, so we can
// use zero as an "inactive" value. It will roll over after 49.7 days.
// Intervals calculated across the zero-cross will be 1 tick too short
// because we skip over 0. But for this application, that's not significant.
volatile uint32_t ticks_cnt;

// Event timers
uint32_t reset_start; // waiting for the end of the reset pulse

ISR(TIMER0_COMPA_vect) {
  static uint8_t cycle_pos = 0;
  // First, set up the timer for the next cycle length.
  if (++cycle_pos >= TICK_DENOMINATOR) cycle_pos = 0;
  // Note that the OCR0A register must be loaded with a value 1 *less*
  // than the actual number of timer clocks you want to count.
  if (cycle_pos >= TICK_NUMERATOR)
    OCR0A = TICK_BASE - 1; // short cycle
  else
    OCR0A = TICK_BASE; // long cycle

  if (++ticks_cnt == 0) ticks_cnt++; // increment it, and disallow 0
}

static uint32_t inline __attribute__ ((always_inline)) ticks() {
  uint32_t out;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    out = ticks_cnt;
  }
  return out;
}

// Read and de-bounce the input. Any positive pulse must last longer
// than DEBOUNCE_TICKS and will result in only one return from this
// method being 1. In order for another pulse to be recognized,
// the input must return low again for DEBOUNCE_TICKS.
static uint8_t read_input() {
  // State for the input debouncer
  static uint32_t debounce_start = 0;
  static uint8_t last_input = 0;

  uint32_t now = ticks();
  uint8_t state = (PINB & IN_PIN_MASK) == 0;
  // If the state has changed, then (re)start the debounce
  if (state != last_input) {
    last_input = state;
    debounce_start = now;
  } else {
    // state has not changed.
    if (debounce_start == 0) return 0; // we're not debouncing
    // If the debounce is now over, then change the state and cancel debounce
    if (now - debounce_start >= DEBOUNCE_TICKS) {
      debounce_start = 0;
      return state; // We just changed the state. Return it.
    }
  }
  return 0;
}

void __ATTR_NORETURN__ main() {

  ADCSRA = 0; // DIE, ADC!!! DIE!!!
  ACSR = _BV(ACD); // Turn off analog comparator - but was it ever on anyway?
  power_adc_disable();
  power_usi_disable();
  power_timer1_disable();

  // set up the output pin - set it low, but everything else high
  // (for pull-ups to reduce power on unconnected pins)
  PORTB = ~OUT_PIN_MASK;
  // Output pin is output, everything else is input.
  DDRB = OUT_PIN_MASK;

  // set up timer 0 to be a millisecond timer.
  TCCR0A = _BV(WGM01); // CTC mode
  TCCR0B = _BV(CS01) | _BV(CS00); // divide by 64, nothing else special
  OCR0A = TICK_BASE; // Start with a long cycle
  TIMSK = _BV(OCIE0A); // interrupt on compare match

  reset_start = 0;

  wdt_enable(WDTO_1S);

  sei(); // release the hounds!

  while(1) {
    wdt_reset(); // pet the dog
    uint32_t now = ticks();

    // If we're resetting the device now, check for the end of the reset pulse
    if (reset_start != 0 && (now - reset_start >= RESET_TICKS) && (PORTB & OUT_PIN_MASK)) {
      PORTB &= ~OUT_PIN_MASK; // power back on
    }

    if (!read_input()) {
      // input is idle.
      // If we're in the holdoff period, check for the end of it
      if (reset_start != 0 && (now - reset_start >= RESET_HOLDOFF))
        reset_start = 0; // we're done with the hold-off.
    } else {
      // input is asserted
      // If we're in the holdoff period, ignore it
      if (reset_start != 0) {
	// Prevent the holdoff period from expiring by dragging the start
	// forward. When we do go high, we don't want to get stuck because
	// of window arithmetic
	if (now - reset_start >= RESET_HOLDOFF) {
		reset_start = now - RESET_HOLDOFF; // slide forward
		if (reset_start == 0) reset_start++; // it can't be zero.
	}
        continue;
      }
      // Start the reset. First, begin the holdoff and the reset pulse
      reset_start = now;
      PORTB |= OUT_PIN_MASK; // begin the reset pulse.
    }
  }
  __builtin_unreachable();
}

