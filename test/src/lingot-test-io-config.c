/*
 * lingot, a musical instrument tuner.
 *
 * Copyright (C) 2013  Iban Cereijo
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

#include "lingot-test.h"

#include "lingot-audio.h"
#include "lingot-config-scale.h"
#include "lingot-io-config.h"

#include <memory.h>

#ifndef LINGOT_TEST_RESOURCES_PATH
#define LINGOT_TEST_RESOURCES_PATH ""
#endif

void lingot_test_io_config(void) {

    lingot_io_config_create_parameter_specs();
    LingotConfig _config;
    LingotConfig* config = &_config;
    lingot_config_new(config);

    CU_ASSERT_PTR_NOT_NULL_FATAL(config);

    // old file with obsolete options
    // ------------------------------

    int ok;
    ok = lingot_io_config_load(config, LINGOT_TEST_RESOURCES_PATH "resources/lingot-001.conf");

    if (ok) { // TODO: assert

        CU_ASSERT_EQUAL(config->audio_system_index, lingot_audio_system_find_by_name("PulseAudio"));
        CU_ASSERT(!strcmp(config->audio_dev[config->audio_system_index],
                  "alsa_input.pci-0000_00_1b.0.analog-stereo"));
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->root_frequency_error, 0.0);
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->fft_size, 512);
        CU_ASSERT_EQUAL(config->min_overall_SNR, 20.0);
        CU_ASSERT_EQUAL(config->calculation_rate, 20.0);
        CU_ASSERT_EQUAL(config->visualization_rate, 30.0);
    } else {
        fprintf(stderr, "warning: cannot find unit test resources\n");
    }

    // recent file
    // -----------

    ok = lingot_io_config_load(config, LINGOT_TEST_RESOURCES_PATH "resources/lingot-0_9_2b8.conf");
    if (ok) { // TODO: assert

#ifdef PULSEAUDIO
        CU_ASSERT_EQUAL(config->audio_system_index, lingot_audio_system_find_by_name("PulseAudio"));
        CU_ASSERT(!strcmp(config->audio_dev[config->audio_system_index], "default"));
#endif
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->root_frequency_error, 0.0);
        CU_ASSERT_EQUAL(config->fft_size, 512);
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->temporal_window, 0.3);
        CU_ASSERT_EQUAL(config->min_overall_SNR, 20.0);
        CU_ASSERT_EQUAL(config->calculation_rate, 15.0);
        CU_ASSERT_EQUAL(config->visualization_rate, 24.0);
        CU_ASSERT_EQUAL(config->min_frequency, 82.41);
        CU_ASSERT_EQUAL(config->max_frequency, 329.63);

        CU_ASSERT(!strcmp(config->scale.name, "Default equal-tempered scale"));
        CU_ASSERT_EQUAL(config->scale.notes, 12);
        CU_ASSERT_EQUAL(config->scale.base_frequency, 261.625565);
        CU_ASSERT(!strcmp(config->scale.note_name[1], "C#"));
        CU_ASSERT(!strcmp(config->scale.note_name[11], "B"));

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][0], 1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][0], 1); // defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[0], 0.0);

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][1], -1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][1], -1); // not defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[1], 100.0);  // defined as shift in cents

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][11], -1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][11], -1); // not defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[11], 1100.0); // defined as shift in cents
    } else {
        fprintf(stderr, "warning: cannot find unit test resources\n");
    }

    ok = lingot_io_config_load(config, LINGOT_TEST_RESOURCES_PATH "resources/lingot-1_0_2b.conf");
    if (ok) { // TODO: assert

#ifdef PULSEAUDIO
        CU_ASSERT_EQUAL(config->audio_system_index, lingot_audio_system_find_by_name("PulseAudio"));
        CU_ASSERT(!strcmp(config->audio_dev[config->audio_system_index], "alsa_input.pci-0000_00_1b.0.analog-stereo"));
#endif
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->root_frequency_error, 0.0);
        CU_ASSERT_EQUAL(config->fft_size, 512);
        CU_ASSERT_EQUAL(config->sample_rate, 44100);
        CU_ASSERT_EQUAL(config->temporal_window, 0.3);
        CU_ASSERT_EQUAL(config->min_overall_SNR, 20.0);
        CU_ASSERT_EQUAL(config->calculation_rate, 15.0);
        CU_ASSERT_EQUAL(config->visualization_rate, 24.0);
        CU_ASSERT_EQUAL(config->min_frequency, 82.41);
        CU_ASSERT_EQUAL(config->max_frequency, 329.63);

        CU_ASSERT(!strcmp(config->scale.name, "Default equal-tempered scale"));
        CU_ASSERT_EQUAL(config->scale.notes, 12);
        CU_ASSERT_EQUAL(config->scale.base_frequency, 261.625565);
        CU_ASSERT(!strcmp(config->scale.note_name[1], "C#"));
        CU_ASSERT(!strcmp(config->scale.note_name[11], "B"));

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][0], 1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][0], 1); // defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[0], 0.0);

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][1], -1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][1], -1); // not defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[1], 100.0);  // defined as shift in cents

        CU_ASSERT_EQUAL(config->scale.offset_ratios[0][11], -1);
        CU_ASSERT_EQUAL(config->scale.offset_ratios[1][11], -1); // not defined as ratio
        CU_ASSERT_EQUAL(config->scale.offset_cents[11], 1100.0); // defined as shift in cents
    } else {
        fprintf(stderr, "warning: cannot find unit test resources\n");
    }

    lingot_config_destroy(config);
}
