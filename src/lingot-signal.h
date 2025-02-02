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

#ifndef LINGOT_SIGNAL_H
#define LINGOT_SIGNAL_H

/*
 peak identification functions.
 */

#include "lingot-defs.h"
#include "lingot-config.h"
#include "lingot-complex.h"
#include "lingot-core.h"

FLT lingot_signal_estimate_fundamental_frequency(const FLT* snr,
                                                 FLT freq,
                                                 const LingotComplex* fft,
                                                 unsigned int N,
                                                 unsigned int n_peaks,
                                                 unsigned int lowest_index,
                                                 unsigned int highest_index,
                                                 unsigned short peak_half_width,
                                                 FLT delta_f_fft,
                                                 FLT min_snr,
                                                 FLT min_q,
                                                 FLT min_freq,
                                                 LingotCore* core,
                                                 short* divisor);

void lingot_signal_compute_noise_level(const FLT* spd,
                                       int N,
                                       int cbuffer_size,
                                       FLT* noise_level);

// generates a Hamming window of N samples
void lingot_signal_window(int N,
                          FLT* out,
                          window_type_t window_type);

#endif /*LINGOT_SIGNAL_H*/
