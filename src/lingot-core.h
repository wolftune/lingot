/*
 * lingot, a musical instrument tuner.
 *
 * Copyright (C) 2004-2019  Iban Cereijo.
 * Copyright (C) 2004-2008  Jairo Chapela.

 *
 * This file is part of lingot.
 *
 * lingot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * lingot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lingot; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef LINGOT_CORE_H
#define LINGOT_CORE_H

#include <pthread.h>

#include "lingot-defs.h"
#include "lingot-complex.h"
#include "lingot-filter.h"
#include "lingot-config.h"

#include "lingot-audio.h"

#include "lingot-fft.h"

typedef struct {

    //  -- shared data --
    FLT freq; // computed analog frequency.
    FLT* SPL; // visual portion of FFT.
    //  -- shared data --

    LingotAudioHandler audio; // audio handler.

    FLT* flt_read_buffer;
    FLT* temporal_buffer; // sample memory.

    // precomputed hamming windows
    FLT* hamming_window_temporal;
    FLT* hamming_window_fft;

    // windowed signals
    FLT* windowed_temporal_buffer;
    FLT* windowed_fft_buffer;

    // spectral power distribution esteem.
    FLT* spd_fft;
    FLT* noise_level;

    LingotFFTPlan fftplan;

    LingotFilter antialiasing_filter; // antialiasing filter for decimation.

    int running;

    LingotConfig conf; // configuration structure

    pthread_t thread_computation;
    pthread_attr_t thread_computation_attr;
    pthread_cond_t thread_computation_cond;
    pthread_mutex_t thread_computation_mutex;

    pthread_mutex_t temporal_buffer_mutex;

#	ifdef DRAW_MARKERS
    int markers[20];
    int markers2[20];
    short markers_size;
    short markers_size2;
#	endif
} LingotCore;

//----------------------------------------------------------------

void lingot_core_new(LingotCore*, LingotConfig*);
void lingot_core_destroy(LingotCore*);

// start process
void lingot_core_start(LingotCore*);

// stop process
void lingot_core_stop(LingotCore*);

// tells whether the two frequencies are harmonically related, giving the
// multipliers to the ground frequency
int lingot_core_frequencies_related(FLT freq1, FLT freq2, FLT minFrequency,
                                    FLT* mulFreq1ToFreq, FLT* mulFreq2ToFreq);

#endif //LINGOT_CORE_H
