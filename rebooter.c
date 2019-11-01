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

// about a second
#define DEBOUNCE_TICKS (1000UL)

// about 2 seconds
#define RESET_TICKS (2000UL)

// about an hour
#define RESET_HOLDOFF (3600UL * 1000)

volatile uint64_t ticks_cnt;
uint64_t debounce_start;
uint8_t last_input;
uint8_t official_state; // this is the "official" state of the input bit

uint64_t reset_holdoff; // waiting after a reset for the input to be idle for an hour
uint64_t reset_start; // the beginning of the reset pulse

ISR(TIMER0_COMPA_vect) {
  if (++ticks_cnt == 0) ticks_cnt++; // increment it, and disallow 0
}

unsigned long inline __attribute__ ((always_inline)) ticks() {
  unsigned long out;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    out = ticks_cnt;
  }
  return out;
}

static uint8_t read_input() {
  uint64_t now = ticks();
  uint8_t state = PINB & IN_PIN_MASK;
  if (state != last_input) {
    last_input = state;
    debounce_start = now;
    return official_state;
  }
  if (debounce_start == 0) return official_state; // we're not debouncing
  if (now - debounce_start >= DEBOUNCE_TICKS) {
    debounce_start = 0;
    official_state = state;
  }
  return official_state;
}

void __ATTR_NORETURN__ main() {

  ADCSRA = 0; // DIE, ADC!!! DIE!!!
  ACSR = _BV(ACD); // Turn off analog comparator - but was it ever on anyway?
  power_adc_disable();
  power_usi_disable();
  power_timer1_disable();

  // set up the output pin - set it low
  PORTB &= ~OUT_PIN_MASK;
  DDRB |= OUT_PIN_MASK;

  // set up the input pin - it's an input, but pulled-up
  PORTB |= IN_PIN_MASK;
  DDRB &= ~IN_PIN_MASK;

  // set up timer 0 to be a millisecond timer.
  TCCR0A = _BV(WGM01); // CTC mode
  TCCR0B = _BV(CS01) | _BV(CS00); // divide by 64, nothing else special
  OCR0A = 15; // 1 MHz divided by 64 divided by 16 is a touch less than 1 kHz.
  TIMSK = _BV(OCIE0A); // interrupt on compare match

  wdt_enable(WDTO_1S);

  official_state = 1; // start with the input NOT asserted
  sei(); // release the hounds!

  while(1) {
    wdt_reset(); // pet the dog
    uint64_t now = ticks();

    if (reset_start != 0 && (now - reset_start >= RESET_TICKS)) {
      PORTB &= ~OUT_PIN_MASK; // deassert
      reset_start = 0;
    }

    if (read_input()) {
      // input is idle.
      if (reset_holdoff != 0 && (now - reset_holdoff >= RESET_HOLDOFF))
        reset_holdoff = 0; // we're done with the hold-off.
    } else {
      // input is asserted
      if (reset_holdoff != 0) {
        reset_holdoff = now; // restart the holdoff interval
        continue;
      }
      reset_holdoff = now;
      reset_start = now;
      PORTB |= OUT_PIN_MASK; // begin the reset pulse.
    }
  }

}

