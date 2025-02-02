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

#include "lingot-gauge.h"

void lingot_gauge_new(LingotGauge* gauge, FLT initial_position) {

    //
    // ----- ERROR GAUGE FILTER CONFIGURATION -----
    //
    // dynamic model of the gauge:
    //
    //               2
    //              d                              d
    //              --- s(t) = k (e(t) - s(t)) - q -- s(t)
    //                2                            dt
    //              dt
    //
    // acceleration of gauge position 's(t)' linearly depends on the difference
    // respect to the input stimulus 'e(t)' (destination position). Inserting
    // a friction coefficient 'q', the acceleration proportionally diminishes with
    // the velocity (typical friction in mechanics). 'k' is the adaptation
    // constant, and depends on the gauge mass.
    //
    // with the following derivative approximations (valid for high sample rate):
    //
    //                 d
    //                 -- s(t) ~= (s[n] - s[n - 1])*fs
    //                 dt
    //
    //            2
    //           d                                            2
    //           --- s(t) ~= (s[n] - 2*s[n - 1] + s[n - 2])*fs
    //             2
    //           dt
    //
    // we can obtain a difference equation, and implement it with an IIR digital
    // filter.
    //

    const FLT k = 60;
    // adaptation constant.
    const FLT q = 6;
    // friction coefficient.

    const FLT a[] = { k + GAUGE_RATE * (q + GAUGE_RATE), -GAUGE_RATE
                      * (q + 2.0 * GAUGE_RATE), GAUGE_RATE * GAUGE_RATE };
    const FLT b[] = { k };

    lingot_filter_new(&gauge->filter, 2, 0, a, b);
    lingot_gauge_compute(gauge, initial_position);
}

void lingot_gauge_destroy(LingotGauge* gauge) {
    lingot_filter_destroy(&gauge->filter);
}

void lingot_gauge_compute(LingotGauge* gauge, FLT sample) {
    gauge->position = lingot_filter_filter_sample(&gauge->filter, sample);
}
