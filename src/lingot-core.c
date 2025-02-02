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

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include "lingot-fft.h"
#include "lingot-signal.h"
#include "lingot-core.h"
#include "lingot-config.h"
#include "lingot-i18n.h"
#include "lingot-msg.h"

void lingot_core_read_callback(FLT* read_buffer, unsigned int samples_read, void *arg);

void* lingot_core_run_computation_thread(void* core);

static unsigned int decimation_input_index = 0;

void lingot_core_new(LingotCore* core, LingotConfig* conf) {

    char buff[1000];

    lingot_config_copy(&core->conf, conf);
    core->running = 0;
    core->spd_fft = NULL;
    core->noise_level = NULL;
    core->SPL = NULL;
    core->flt_read_buffer = NULL;
    core->temporal_buffer = NULL;
    core->windowed_temporal_buffer = NULL;
    core->windowed_fft_buffer = NULL;
    core->hamming_window_temporal = NULL;
    core->hamming_window_fft = NULL;

#ifdef DRAW_MARKERS
    core->markers_size = 0;
    core->markers_size2 = 0;
#endif

    unsigned int requested_sample_rate = core->conf.sample_rate;

    if (core->conf.sample_rate <= 0) {
        core->conf.sample_rate = 0;
    }

    lingot_audio_new(&core->audio,
                     core->conf.audio_system_index,
                     core->conf.audio_dev[core->conf.audio_system_index],
            core->conf.sample_rate, lingot_core_read_callback, core);

    if (core->audio.audio_system != -1) {

        if (requested_sample_rate != core->audio.real_sample_rate) {
            core->conf.sample_rate = core->audio.real_sample_rate;
            lingot_config_update_internal_params(conf);
            //			sprintf(buff,
            //					_("The requested sample rate is not available, the real sample rate has been set to %d Hz"),
            //					core->audio.real_sample_rate);
            //			lingot_msg_add_warning(buff);
        }

        if (core->conf.temporal_buffer_size < core->conf.fft_size) {
            core->conf.temporal_window = ((double) core->conf.fft_size
                                          * core->conf.oversampling) / core->conf.sample_rate;
            core->conf.temporal_buffer_size = core->conf.fft_size;
            lingot_config_update_internal_params(conf);
            snprintf(buff, sizeof(buff),
                     _("The temporal buffer is smaller than FFT size. It has been increased to %0.3f seconds"),
                     core->conf.temporal_window);
            lingot_msg_add_warning(buff);
        }

        // Since the SPD is symmetrical, we only store the 1st half.
        const unsigned int spd_size = (core->conf.fft_size / 2);

        core->spd_fft = malloc(spd_size * sizeof(FLT));
        core->noise_level = malloc(spd_size * sizeof(FLT));
        core->SPL = malloc(spd_size * sizeof(FLT));

        memset(core->spd_fft, 0, spd_size * sizeof(FLT));
        memset(core->noise_level, 0, spd_size * sizeof(FLT));
        memset(core->SPL, 0, spd_size * sizeof(FLT));

        // audio source read in floating point format.
        core->flt_read_buffer = malloc(
                    core->audio.read_buffer_size_samples * sizeof(FLT));
        memset(core->flt_read_buffer, 0,
               core->audio.read_buffer_size_samples * sizeof(FLT));

        // stored samples.
        core->temporal_buffer = malloc(
                    (core->conf.temporal_buffer_size) * sizeof(FLT));
        memset(core->temporal_buffer, 0,
               core->conf.temporal_buffer_size * sizeof(FLT));

        core->hamming_window_temporal = NULL;
        core->hamming_window_fft = NULL;

        if (core->conf.window_type != NONE) {
            core->hamming_window_temporal = malloc(
                        (core->conf.temporal_buffer_size) * sizeof(FLT));
            core->hamming_window_fft = malloc(
                        (core->conf.fft_size) * sizeof(FLT));

            lingot_signal_window(core->conf.temporal_buffer_size,
                                 core->hamming_window_temporal, core->conf.window_type);
            lingot_signal_window(core->conf.fft_size, core->hamming_window_fft,
                                 core->conf.window_type);
        }

        core->windowed_temporal_buffer = malloc(
                    (core->conf.temporal_buffer_size) * sizeof(FLT));
        memset(core->windowed_temporal_buffer, 0,
               core->conf.temporal_buffer_size * sizeof(FLT));
        core->windowed_fft_buffer = malloc(
                    (core->conf.fft_size) * sizeof(FLT));
        memset(core->windowed_fft_buffer, 0,
               core->conf.fft_size * sizeof(FLT));

        lingot_fft_plan_create(&core->fftplan, core->windowed_fft_buffer,
                               core->conf.fft_size);

        /*
         * 8 order Chebyshev filters, with wc=0.9/i (normalised respect to
         * Pi). We take 0.9 instead of 1 to leave a 10% of safety margin,
         * in order to avoid aliased frequencies near to w=Pi, due to non
         * ideality of the filter.
         *
         * The cut frequencies wc=Pi/i, with i=1..20, correspond with the
         * oversampling factor, avoiding aliasing at decimation.
         *
         * Why Chebyshev filters?, for a given order, those filters yield
         * abrupt falls than other ones as Butterworth, making the most of
         * the order. Although Chebyshev filters affect more to the phase,
         * it doesn't matter due to the analysis is made on the signal
         * power distribution (only magnitude).
         */
        lingot_filter_cheby_design(&core->antialiasing_filter, 8, 0.5,
                                   0.9 / core->conf.oversampling);

        pthread_mutex_init(&core->temporal_buffer_mutex, NULL);

        // ------------------------------------------------------------

        core->running = 1;
    }

    core->freq = 0.0;
}

// -----------------------------------------------------------------------

/* Deallocate resources */
void lingot_core_destroy(LingotCore* core) {

    if (core->audio.audio_system != -1) {
        lingot_fft_plan_destroy(&core->fftplan);
        lingot_audio_destroy(&core->audio);

        free(core->spd_fft);
        free(core->noise_level);
        free(core->SPL);
        free(core->flt_read_buffer);
        free(core->temporal_buffer);

        free(core->hamming_window_temporal);
        free(core->windowed_temporal_buffer);
        free(core->hamming_window_fft);
        free(core->windowed_fft_buffer);

        lingot_filter_destroy(&core->antialiasing_filter);

        pthread_mutex_destroy(&core->temporal_buffer_mutex);
    }
}

// -----------------------------------------------------------------------

// reads a new piece of signal from audio source, applies filtering and
// decimation and appends it to the buffer
void lingot_core_read_callback(FLT* read_buffer, unsigned int samples_read, void *arg) {

    unsigned int decimation_output_index; // loop variables.
    unsigned int decimation_output_len;
    FLT* decimation_in;
    FLT* decimation_out;
    LingotCore* core = (LingotCore*) arg;
    const LingotConfig* const conf = &core->conf;

    memcpy(core->flt_read_buffer, read_buffer, samples_read * sizeof(FLT));
    //	double omega = 2.0 * M_PI * 100.0;
    //	double T = 1.0 / conf->sample_rate;
    //	static double t = 0.0;
    //	for (i = 0; i < read_buffer_size; i++) {
    //		core->flt_read_buffer[i] = 1e4
    //				* (cos(omega * t) + (0.5 * rand()) / RAND_MAX);
    //		t += T;
    //	}

    //	if (conf->gain_nu != 1.0) {
    //		for (i = 0; i < read_buffer_size; i++)
    //			core->flt_read_buffer[i] *= conf->gain_nu;
    //	}

    //
    // just read:
    //
    //  ----------------------------
    // |bxxxbxxxbxxxbxxxbxxxbxxxbxxx|
    //  ----------------------------
    //
    // <----------------------------> samples_read
    //

    decimation_output_len = 1
            + (samples_read - (decimation_input_index + 1))
            / conf->oversampling;

    //#define DUMP

#ifdef DUMP
    static FILE* fid0 = 0x0;
    if (fid0 == 0x0) {
        fid0 = fopen("/tmp/dump_pre_filter.txt", "w");
    }

    for (i = 0; i < samples_read; i++) {
        fprintf(fid0, "%f ", core->flt_read_buffer[i]);
    }
#endif

    pthread_mutex_lock(&core->temporal_buffer_mutex);

    /* we shift the temporal window to leave a hollow where place the new piece
     of data read. The buffer is actually a queue. */
    if (conf->temporal_buffer_size > decimation_output_len) {
        memmove(core->temporal_buffer,
                &core->temporal_buffer[decimation_output_len],
                (conf->temporal_buffer_size - decimation_output_len)
                * sizeof(FLT));
    }

    //
    // previous buffer situation:
    //
    //  ------------------------------------------
    // | xxxxxxxxxxxxxxxxxxxxxx | yyyyy | aaaaaaa |
    //  ------------------------------------------
    //                                    <------> samples_read/oversampling
    //                           <---------------> fft_size
    //  <----------------------------------------> temporal_buffer_size
    //
    // new situation:
    //
    //  ------------------------------------------
    // | xxxxxxxxxxxxxxxxyyyyaa | aaaaa |         |
    //  ------------------------------------------
    //

    // decimation with low-pass filtering

    /* we decimate the signal and append it at the end of the buffer. */
    if (conf->oversampling > 1) {

        decimation_in = core->flt_read_buffer;
        decimation_out = &core->temporal_buffer[conf->temporal_buffer_size
                - decimation_output_len];

        // low pass filter to avoid aliasing.
        lingot_filter_filter(&core->antialiasing_filter, samples_read,
                             decimation_in, decimation_in);

        // downsampling.
        for (decimation_output_index = 0; decimation_input_index < samples_read;
             decimation_output_index++, decimation_input_index +=
             conf->oversampling) {
            decimation_out[decimation_output_index] =
                    decimation_in[decimation_input_index];
        }
        decimation_input_index -= samples_read;
    } else {
        memcpy(
                    &core->temporal_buffer[conf->temporal_buffer_size
                - decimation_output_len], core->flt_read_buffer,
                decimation_output_len * sizeof(FLT));
    }
    //
    //  ------------------------------------------
    // | xxxxxxxxxxxxxxxxyyyyaa | aaaaa | bbbbbbb |
    //  ------------------------------------------
    //

    pthread_mutex_unlock(&core->temporal_buffer_mutex);

#ifdef DUMP
    static FILE* fid1 = 0x0;
    static FILE* fid2 = 0x0;

    if (fid1 == 0x0) {
        fid1 = fopen("/tmp/dump_post_filter.txt", "w");
        fid2 = fopen("/tmp/dump_post.txt", "w");
    }

    for (i = 0; i < samples_read; i++) {
        fprintf(fid1, "%f ", core->flt_read_buffer[i]);
    }
    decimation_out = &core->temporal_buffer[conf->temporal_buffer_size
            - decimation_output_len];
    for (i = 0; i < decimation_output_len; i++) {
        fprintf(fid2, "%f ", decimation_out[i]);
    }
#endif
}

int lingot_core_frequencies_related(FLT freq1, FLT freq2, FLT minFrequency,
                                    FLT* mulFreq1ToFreq, FLT* mulFreq2ToFreq) {

    int result = 0;
    static const FLT tol = 5e-2; // TODO: tune
    static const int maxDivisor = 4;

    if ((freq1 != 0.0) && (freq2 != 0.0)) {

        FLT smallFreq = freq1;
        FLT bigFreq = freq2;

        if (bigFreq < smallFreq) {
            smallFreq = freq2;
            bigFreq = freq1;
        }

        int divisor;
        FLT frac;
        FLT error = -1.0;
        for (divisor = 1; divisor <= maxDivisor; divisor++) {
            if (minFrequency * divisor > smallFreq) {
                break;
            }

            frac = bigFreq * divisor / smallFreq;
            error = fabs(frac - round(frac));
            if (error < tol) {
                if (smallFreq == freq1) {
                    *mulFreq1ToFreq = 1.0 / divisor;
                    *mulFreq2ToFreq = 1.0 / round(frac);
                } else {
                    *mulFreq1ToFreq = 1.0 / round(frac);
                    *mulFreq2ToFreq = 1.0 / divisor;
                }
                result = 1;
                break;
            }
        }
    } else {
        *mulFreq1ToFreq = 0.0;
        *mulFreq2ToFreq = 0.0;
    }

    //	printf("relation %f, %f = %i\n", freq1, freq2, result);

    return result;
}

static FLT lingot_core_frequency_locker(FLT freq, FLT minFrequency) {

    static int locked = 0;
    static FLT current_frequency = -1.0;
    static int hits_counter = 0;
    static int rehits_counter = 0;
    static int rehits_up_counter = 0;
    static const int nhits_to_lock = 4;
    static const int nhits_to_unlock = 5;
    static const int nhits_to_relock = 6;
    static const int nhits_to_relock_up = 8;
    FLT multiplier = 0.0;
    FLT multiplier2 = 0.0;
    static FLT old_multiplier = 0.0;
    static FLT old_multiplier2 = 0.0;
    int fail = 0;
    FLT result = 0.0;

#ifdef DRAW_MARKERS
    printf("f = %f\n", freq);
#endif
    int consistent_with_current_frequency = 0;
    consistent_with_current_frequency = lingot_core_frequencies_related(freq,
                                                                        current_frequency, minFrequency, &multiplier, &multiplier2);

    if (!locked) {

        if ((freq > 0.0) && (current_frequency == 0.0)) {
            consistent_with_current_frequency = 1;
            multiplier = 1.0;
            multiplier2 = 1.0;
        }

        //		printf("filtering frequency %f, current %f\n", freq, current_frequency);

        if (consistent_with_current_frequency && (multiplier == 1.0)
                && (multiplier2 == 1.0)) {
            current_frequency = freq * multiplier;

            if (++hits_counter >= nhits_to_lock) {
                locked = 1;
#ifdef DRAW_MARKERS
                printf("locked to frequency %f\n", current_frequency);
#endif
                hits_counter = 0;
            }
        } else {
            hits_counter = 0;
            current_frequency = 0.0;
        }

        //		result = freq;
    } else {
        //		printf("c = %i, f = %f, cf = %f, multiplier = %f, multiplier2 = %f\n",
        //				consistent_with_current_frequency, freq, current_frequency,
        //				multiplier, multiplier2);

        if (consistent_with_current_frequency) {
            if (fabs(multiplier2 - 1.0) < 1e-5) {
                result = freq * multiplier;
                current_frequency = result;
                rehits_counter = 0;

                if (fabs(multiplier - 1.0) > 1e-5) {
                    if (fabs(multiplier - old_multiplier) < 1e-5) {
#ifdef DRAW_MARKERS
                        printf("SEIN!!!! %f!\n", multiplier);
#endif
                        if (++rehits_up_counter >= nhits_to_relock_up) {
                            result = freq;
                            current_frequency = result;
#ifdef DRAW_MARKERS
                            printf("relock UP!! to %f\n\n\n", freq);
#endif
                            rehits_up_counter = 0;
                            fail = 0;
                        }
                    } else {
                        rehits_up_counter = 0;
                    }
                } else {
                    rehits_up_counter = 0;
                }
            } else {
                rehits_up_counter = 0;
#ifdef DRAW_MARKERS
                printf("%f!\n", multiplier2);
#endif
                if (fabs(multiplier2 - 0.5) < 1e-5) {
                    hits_counter--;
                }
                fail = 1;
                if (freq * multiplier < minFrequency) {
#ifdef DRAW_MARKERS
                    printf("warning freq * mul = %f < min\n",
                           freq * multiplier);
#endif
                } else {
                    //					result = freq * multiplier;
                    //					printf("hop detected!\n");
                    //					current_frequency = result;

#ifdef DRAW_MARKERS
                    printf("(%f == %f)?\n", multiplier2, old_multiplier2);
#endif
                    if (fabs(multiplier2 - old_multiplier2) < 1e-5) {
#ifdef DRAW_MARKERS
                        printf("match for relock, %f == %f\n", multiplier2,
                               old_multiplier2);
#endif
                        if (++rehits_counter >= nhits_to_relock) {
                            result = freq * multiplier;
                            current_frequency = result;
#ifdef DRAW_MARKERS
                            printf("relock!! to %f\n", freq);
#endif
                            rehits_counter = 0;
                            fail = 0;
                        }
                    }
                }
            }

        } else {
            fail = 1;
        }

        if (fail) {
            result = current_frequency;
            hits_counter++;
            if (hits_counter >= nhits_to_unlock) {
                current_frequency = 0.0;
                locked = 0;
                hits_counter = 0;
#ifdef DRAW_MARKERS
                printf("unlocked\n");
#endif
                result = 0.0;
            }
        } else {
            hits_counter = 0;
        }
    }

    old_multiplier = multiplier;
    old_multiplier2 = multiplier2;

    //	if (result != 0.0)
    //		printf("result = %f\n", result);
    return result;
}

void lingot_core_compute_fundamental_fequency(LingotCore* core) {

    register unsigned int i, k; // loop variables.
    const LingotConfig* const conf = &core->conf;
    const FLT index2f = ((FLT) conf->sample_rate)
            / (conf->oversampling * conf->fft_size); // FFT resolution in Hz.
    //	const FLT index2w = 2.0 * M_PI / conf->fft_size; // FFT resolution in rads.
    //	const FLT f2w = 2 * M_PI * conf->oversampling / conf->sample_rate;
    //	const FLT w2f = 1.0 / f2w;

    // ----------------- TRANSFORMATION TO FREQUENCY DOMAIN ----------------

    pthread_mutex_lock(&core->temporal_buffer_mutex);

    // windowing
    if (conf->window_type != NONE) {
        for (i = 0; i < conf->fft_size; i++) {
            core->windowed_fft_buffer[i] =
                    core->temporal_buffer[conf->temporal_buffer_size
                    - conf->fft_size + i] * core->hamming_window_fft[i];
        }
    } else {
        memmove(core->windowed_fft_buffer,
                &core->temporal_buffer[conf->temporal_buffer_size
                - conf->fft_size], conf->fft_size * sizeof(FLT));
    }

    const unsigned int spd_size = (conf->fft_size / 2);

    // FFT
    lingot_fft_compute_dft_and_spd(&core->fftplan, core->spd_fft, spd_size);

    static const FLT minSPL = -200;
    for (i = 0; i < spd_size; i++) {
        core->SPL[i] = 10.0 * log10(core->spd_fft[i]);
        if (core->SPL[i] < minSPL) {
            core->SPL[i] = minSPL;
        }
    }

    FLT noise_filter_width = 150.0; // hz
    unsigned int noise_filter_width_samples = ceil(
                noise_filter_width * conf->fft_size * conf->oversampling
                / conf->sample_rate);

    lingot_signal_compute_noise_level(core->SPL, spd_size,
                                      noise_filter_width_samples, core->noise_level);
    for (i = 0; i < spd_size; i++) {
        core->SPL[i] -= core->noise_level[i];
    }

    unsigned int lowest_index = (unsigned int) ceil(
                conf->internal_min_frequency
                * (1.0 * conf->oversampling / conf->sample_rate)
                * conf->fft_size);
    unsigned int highest_index = (unsigned int) ceil(0.95 * spd_size);

    short divisor = 1;
    FLT f0 = lingot_signal_estimate_fundamental_frequency(core->SPL,
                                                          0.5 * core->freq,
                                                          (const LingotComplex*) core->fftplan.fft_out,
                                                          spd_size,
                                                          conf->peak_number,
                                                          lowest_index,
                                                          highest_index,
                                                          (unsigned short) conf->peak_half_width,
                                                          index2f,
                                                          conf->min_SNR,
                                                          conf->min_overall_SNR,
                                                          conf->internal_min_frequency,
                                                          core,
                                                          &divisor);

    FLT w;
    FLT w0 =
            (f0 == 0.0) ?
                0.0 :
                2 * M_PI * f0 * conf->oversampling / conf->sample_rate;
    w = w0;
    //	int Mi = floor(w / index2w);

    if (w != 0.0) {
        // windowing
        if (conf->window_type != NONE) {
            for (i = 0; i < conf->temporal_buffer_size; i++) {
                core->windowed_temporal_buffer[i] = core->temporal_buffer[i]
                        * core->hamming_window_temporal[i];
            }
        } else {
            memmove(core->windowed_temporal_buffer, core->temporal_buffer,
                    conf->temporal_buffer_size * sizeof(FLT));
        }
    }

    pthread_mutex_unlock(&core->temporal_buffer_mutex); // we don't need the read buffer anymore

    if (w != 0.0) {

        //  Maximum finding by Newton-Raphson
        // -----------------------------------

        FLT wk = -1.0e5;
        FLT wkm1 = w;
        // first iterator set to the current approximation.
        FLT d0_SPD = 0.0;
        FLT d1_SPD = 0.0;
        FLT d2_SPD = 0.0;
        FLT d0_SPD_old = 0.0;

        //		printf("NR iter: %f ", w * w2f);

        for (k = 0; (k < conf->max_nr_iter) && (fabs(wk - wkm1) > 1.0e-4);
             k++) {
            wk = wkm1;

            d0_SPD_old = d0_SPD;
            lingot_fft_spd_diffs_eval(core->windowed_fft_buffer, // TODO: iterate over this buffer?
                                      conf->fft_size, wk, &d0_SPD, &d1_SPD, &d2_SPD);

            wkm1 = wk - d1_SPD / d2_SPD;
            //			printf(" -> (%f,%g,%g,%g)", wkm1 * w2f, d0_SPD, d1_SPD, d2_SPD);

            if (d0_SPD < d0_SPD_old) {
                //				printf("!!!", d0_SPD, d0_SPD_old);
                wkm1 = 0.0;
                break;
            }

        }
        //		printf("\n");

        if (wkm1 > 0.0) {
            w = wkm1; // frequency in rads.
            wk = -1.0e5;
            d0_SPD = 0.0;
            //			printf("NR2 iter: %f ", w * w2f);

            for (k = 0;
                 (k <= 1)
                 || ((k < conf->max_nr_iter)
                     && (fabs(wk - wkm1) > 1.0e-4)); k++) {
                wk = wkm1;

                // ! we use the WHOLE temporal window for bigger precision.
                d0_SPD_old = d0_SPD;
                lingot_fft_spd_diffs_eval(core->windowed_temporal_buffer,
                                          conf->temporal_buffer_size, wk, &d0_SPD, &d1_SPD,
                                          &d2_SPD);

                wkm1 = wk - d1_SPD / d2_SPD;
                //				printf(" -> (%f,%g,%g,%g)", wkm1 * w2f, d0_SPD, d1_SPD, d2_SPD);

                if (d0_SPD < d0_SPD_old) {
                    //					printf("!!!");
                    wkm1 = 0.0;
                    break;
                }

            }
            //			printf("\n");

            if (wkm1 > 0.0) {
                w = wkm1; // frequency in rads.
            }
        }
    }

    FLT freq =
            (w == 0.0) ?
                0.0 :
                w * conf->sample_rate
                / (divisor * 2.0 * M_PI * conf->oversampling); // analog frequency in Hz.
    //	core->freq = freq;
    core->freq = lingot_core_frequency_locker(freq,
                                              core->conf.internal_min_frequency);
    //	printf("-> %f\n", core->freq);
}

/* start running the core in another thread */
void lingot_core_start(LingotCore* core) {

    int audio_status = 0;
    decimation_input_index = 0;

    if (core->audio.audio_system != -1) {
        audio_status = lingot_audio_start(&core->audio);

        if (audio_status == 0) {
            pthread_mutex_init(&core->thread_computation_mutex, NULL);
            pthread_cond_init(&core->thread_computation_cond, NULL);

            pthread_attr_init(&core->thread_computation_attr);
            pthread_create(&core->thread_computation,
                           &core->thread_computation_attr,
                           lingot_core_run_computation_thread,
                           core);
            core->running = 1;
        } else {
            core->running = 0;
            lingot_audio_destroy(&core->audio);
        }

    }
}

/* stop running the core */
void lingot_core_stop(LingotCore* core) {
    void* thread_result;

    int result;
    struct timeval tout_abs;
    struct timespec tout_tspec;

    gettimeofday(&tout_abs, NULL);

    if (core->running == 1) {
        core->running = 0;

        tout_abs.tv_usec += 300000;
        if (tout_abs.tv_usec >= 1000000) {
            tout_abs.tv_usec -= 1000000;
            tout_abs.tv_sec++;
        }

        tout_tspec.tv_sec = tout_abs.tv_sec;
        tout_tspec.tv_nsec = 1000 * tout_abs.tv_usec;

        // watchdog timer
        pthread_mutex_lock(&core->thread_computation_mutex);
        result = pthread_cond_timedwait(&core->thread_computation_cond,
                                        &core->thread_computation_mutex, &tout_tspec);
        pthread_mutex_unlock(&core->thread_computation_mutex);

        if (result == ETIMEDOUT) {
            fprintf(stderr, "warning: cancelling computation thread\n");
            //			pthread_cancel(core->thread_computation);
        } else {
            pthread_join(core->thread_computation, &thread_result);
        }
        pthread_attr_destroy(&core->thread_computation_attr);
        pthread_mutex_destroy(&core->thread_computation_mutex);
        pthread_cond_destroy(&core->thread_computation_cond);

        int spd_size = core->conf.fft_size / 2;
        memset(core->SPL, 0, spd_size * sizeof(FLT));
        core->freq = 0.0;
    }

    if (core->audio.audio_system != -1) {
        lingot_audio_stop(&core->audio);
    }
}

/* run the core */
void* lingot_core_run_computation_thread(void* _core) {
    struct timeval tout_abs;
    struct timespec tout_tspec;

    LingotCore* core = _core;
    gettimeofday(&tout_abs, NULL);

    while (core->running) {
        lingot_core_compute_fundamental_fequency(core);
        tout_abs.tv_usec += 1e6 / core->conf.calculation_rate;
        if (tout_abs.tv_usec >= 1000000) {
            tout_abs.tv_usec -= 1000000;
            tout_abs.tv_sec++;
        }
        tout_tspec.tv_sec = tout_abs.tv_sec;
        tout_tspec.tv_nsec = 1000 * tout_abs.tv_usec;
        pthread_mutex_lock(&core->thread_computation_mutex);
        pthread_cond_timedwait(&core->thread_computation_cond,
                               &core->thread_computation_mutex, &tout_tspec);
        pthread_mutex_unlock(&core->thread_computation_mutex);

        if (core->audio.audio_system != -1) {
            const unsigned int spd_size = core->conf.fft_size / 2;
            if (core->audio.interrupted) {
                memset(core->SPL, 0, spd_size * sizeof(FLT));
                core->freq = 0.0;
                core->running = 0;
            }
        }
    }

    pthread_mutex_lock(&core->thread_computation_mutex);
    pthread_cond_broadcast(&core->thread_computation_cond);
    pthread_mutex_unlock(&core->thread_computation_mutex);

    return NULL;
}
