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

#define F_CPU (1000000UL)

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdlib.h>
#include <stdint.h>

// 1 MHz / 8 is 125,000. To get a 1/10th sec timer out of that,
// divide that by 1000, which is 12,500. But subtract one because it's
// zero based and inclusive.

#define BASE (12500 - 1)
// If we ever need fractional counting, this allows for it.
#define CYCLE_COUNT (0)
#define LONG_CYCLES (0)

// Where is the hardware?
#if defined (__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined (__AVR_ATtiny85__)
#define IN_PIN_MASK _BV(4)
#define OUT_PIN_MASK _BV(3)
#else
#define IN_PIN_MASK _BV(0)
#define OUT_PIN_MASK _BV(2)
#endif

// This is how long the input has to stay at a particular
// state for a change to be recognized.
// about a second
#define DEBOUNCE_TICKS (10U)

// This is how long the power is turned off when the input is asserted
// about 10 seconds
#define RESET_TICKS (100U)

// This is how long subsequente reset pulses will be ignored after one is
// recognized and the power is cycled.
// about an hour
#define RESET_HOLDOFF (3600U * 10)

// Tick counter. This is not allowed to equal zero, so we can
// use zero as an "inactive" value. It will roll over after 1.8 hours.
// Intervals calculated across the zero-cross will be 1 tick too short
// because we skip over 0. But for this application, that's not significant.
volatile uint16_t ticks_cnt;

// Event timers
uint16_t reset_start; // waiting for the end of the reset pulse

#if defined (__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined (__AVR_ATtiny85__)
ISR(TIMER0_COMPA_vect) {
#else
ISR(TIM0_COMPA_vect) {
#endif
        if (++ticks_cnt == 0) ticks_cnt++; // it can't be zero
#if (CYCLE_COUNT > 0)
        static unsigned int cycle_pos;
        if (cycle_pos++ >= LONG_CYCLES) {
                OCR0A = BASE;
        } else {
                OCR0A = BASE + 1;
        }
        if (cycle_pos == CYCLE_COUNT) cycle_pos = 0;
#endif
}

static uint16_t inline __attribute__ ((always_inline)) ticks() {
  uint16_t out;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    out = ticks_cnt;
  }
  return out;
}

// Read and de-bounce the input. Any positive pulse must last longer
// than DEBOUNCE_TICKS and will result in only one return from this
// method being 1. In order for another pulse to be recognized,
// the input must return low again for DEBOUNCE_TICKS.
static inline __attribute__ ((always_inline)) uint8_t read_input() {
  // State for the input debouncer
  static uint16_t debounce_start = 0;
  static uint8_t last_input = 0;

  uint16_t now = ticks();
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

  wdt_enable(WDTO_500MS);

  ACSR = _BV(ACD); // Turn off analog comparator
  power_adc_disable();
#if defined (__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined (__AVR_ATtiny85__)
  power_usi_disable();
  power_timer1_disable();
#endif

  // set up the output pin - set it low, but everything else high
  // (for pull-ups to reduce power on unconnected pins)
#if defined (__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined (__AVR_ATtiny85__)
  PORTB = ~OUT_PIN_MASK;
#else
  // tiny9 uses a pull-up register
  PORTB = 0;
  PUEB = ~OUT_PIN_MASK;
#endif
  // Output pin is output, everything else is input.
  DDRB = OUT_PIN_MASK;

  // set up timer 0 to be a tick timer.
#if defined (__AVR_ATtiny25__) | defined(__AVR_ATtiny45__) | defined (__AVR_ATtiny85__)
  TCCR0A = _BV(WGM01); // CTC mode
  TCCR0B = _BV(CS01); // divide by 8, nothing else special
  TIMSK = _BV(OCIE0A); // interrupt on compare match
#else
  TCCR0A = 0;
  TCCR0B = _BV(WGM02) | _BV(CS01); // prescale by 8, CTC mode
  TIMSK0 = _BV(OCIE0A); // OCR0A interrupt only.
#endif
#if (CYCLE_COUNT > 0)
        OCR0A = BASE + 1; // long cycle
#else
        OCR0A = BASE; // short cycle
#endif

  reset_start = 0;

  sei(); // release the hounds!

  while(1) {
    wdt_reset(); // pet the dog
    uint16_t now = ticks();

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

