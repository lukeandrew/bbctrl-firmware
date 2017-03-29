/******************************************************************************\

                This file is part of the Buildbotics firmware.

                  Copyright (c) 2015 - 2017 Buildbotics LLC
                  Copyright (c) 2010 - 2015 Alden S. Hart, Jr.
                  Copyright (c) 2013 - 2015 Robert Giseburt
                            All rights reserved.

     This file ("the software") is free software: you can redistribute it
     and/or modify it under the terms of the GNU General Public License,
      version 2 as published by the Free Software Foundation. You should
      have received a copy of the GNU General Public License, version 2
     along with the software. If not, see <http://www.gnu.org/licenses/>.

     The software is distributed in the hope that it will be useful, but
          WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
               Lesser General Public License for more details.

       You should have received a copy of the GNU Lesser General Public
                License along with the software.  If not, see
                       <http://www.gnu.org/licenses/>.

                For information regarding this software email:
                  "Joseph Coffland" <joseph@buildbotics.com>

\******************************************************************************/

#include "stepper.h"

#include "config.h"
#include "machine.h"
#include "plan/runtime.h"
#include "plan/exec.h"
#include "motor.h"
#include "hardware.h"
#include "estop.h"
#include "util.h"
#include "cpp_magic.h"

#include <string.h>
#include <stdio.h>


typedef enum {
  MOVE_TYPE_NULL,          // null move - does a no-op
  MOVE_TYPE_LINE,         // acceleration planned line
  MOVE_TYPE_DWELL,         // delay with no movement
} move_type_t;


typedef struct {
  // Runtime
  bool busy;
  bool requesting;
  float dwell;

  // Move prep
  bool move_ready;         // prepped move ready for loader
  bool move_queued;        // prepped move queued
  move_type_t move_type;
  float prep_dwell;
} stepper_t;


static stepper_t st = {0};


void stepper_init() {
  // Setup step timer
  TIMER_STEP.CTRLB = STEP_TIMER_WGMODE;    // waveform mode
  TIMER_STEP.INTCTRLA = STEP_TIMER_INTLVL; // interrupt mode
  TIMER_STEP.PER = STEP_TIMER_POLL;        // timer idle rate
  TIMER_STEP.CTRLA = STEP_TIMER_ENABLE;    // start step timer
}


void st_shutdown() {
  for (int motor = 0; motor < MOTORS; motor++)
    motor_enable(motor, false);

  st.dwell = 0;
  st.move_type = MOVE_TYPE_NULL;
}


/// Return true if motors or dwell are running
bool st_is_busy() {return st.busy;}


/// Interrupt handler for calling move exec function.
/// ADC channel 0 triggered by load ISR as a "software" interrupt.
ISR(ADCB_CH0_vect) {
  while (true) {
    stat_t status = mp_exec_move();

    switch (status) {
    case STAT_NOOP: st.busy = false;  break; // No command executed
    case STAT_EAGAIN: continue;              // No command executed, try again

    case STAT_OK:                            // Move executed
      if (!st.move_queued) ALARM(STAT_EXPECTED_MOVE); // No move was queued
      st.move_queued = false;
      st.move_ready = true;
      break;

    default: ALARM(status); break;
    }

    break;
  }

  ADCB_CH0_INTCTRL = 0;
  st.requesting = false;
}


static void _request_exec_move() {
  if (st.requesting) return;
  st.requesting = true;

  // Use ADC as a "software" interrupt to trigger next move exec
  ADCB_CH0_INTCTRL = ADC_CH_INTLVL_LO_gc; // LO level interrupt
  ADCB_CTRLA = ADC_ENABLE_bm | ADC_CH0START_bm;
}


/// Step timer interrupt routine
/// Dequeue move and load into stepper struct
ISR(STEP_TIMER_ISR) {
  // Dwell
  if (0 < st.dwell) {
    st.dwell -= SEGMENT_SEC;
    return;
  } else st.dwell = 0;

  // Default clock rate
  TIMER_STEP.PER = STEP_TIMER_POLL;

  if (estop_triggered()) {
    st.move_type = MOVE_TYPE_NULL;
    return;
  }

  // If the next move is not ready try to load it
  if (!st.move_ready) {
    _request_exec_move();
    return;
  }

  // Wait until all motors have energized
  if (motor_energizing()) return;

  // Start move
  if (st.move_type == MOVE_TYPE_LINE)
    for (int motor = 0; motor < MOTORS; motor++)
      motor_load_move(motor);

  if (st.move_type != MOVE_TYPE_NULL) {
    TIMER_STEP.PER = SEGMENT_PERIOD;
    st.busy = true;

    // Start dwell
    st.dwell = st.prep_dwell;
  }

  // We are done with this move
  st.move_type = MOVE_TYPE_NULL;
  st.prep_dwell = 0;      // clear dwell
  st.move_ready = false;  // flip the flag back

  // Request next move if not currently in a dwell.  Requesting the next move
  // may power up motors and the motors should not be powered up during a dwell.
  if (!st.dwell) _request_exec_move();
}


/* Prepare the next move
 *
 * This function precomputes the next pulse segment (move) so it can
 * be executed quickly in the ISR.  It works in steps, rather than
 * length units.  All args are provided as floats which converted here
 * to integer values.
 *
 * Args:
 *   @param target signed position in steps for each motor.
 *   Steps are fractional.  Their sign indicates direction.  Motors not in the
 *   move have 0 steps.
 */
stat_t st_prep_line(const float target[]) {
  // Trap conditions that would prevent queueing the line
  if (st.move_ready) return ALARM(STAT_INTERNAL_ERROR);

  // Setup segment parameters
  st.move_type = MOVE_TYPE_LINE;

  // Prepare motor moves
  for (int motor = 0; motor < MOTORS; motor++)
    RITORNO(motor_prep_move(motor, round(target[motor])));

  st.move_queued = true; // signal prep buffer ready (do this last)

  return STAT_OK;
}


/// Add a dwell to the move buffer
void st_prep_dwell(float seconds) {
  if (st.move_ready) ALARM(STAT_INTERNAL_ERROR);
  st.move_type = MOVE_TYPE_DWELL;
  st.prep_dwell = seconds;
  st.move_queued = true; // signal prep buffer ready
}