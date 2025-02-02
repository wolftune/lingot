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

void lingot_test_io_config(void);
void lingot_test_config_scale(void);
void lingot_test_signal(void);
void lingot_test_core(void);

#ifndef LINGOT_TEST_USE_LIB

// TODO: lib?
#include "lingot-complex.c"
#include "lingot-msg.c"
#include "lingot-config-scale.c"
#include "lingot-config.c"
#include "lingot-io-config.c"
#include "lingot-io-config-scale.c"
#include "lingot-audio.c"
#include "lingot-audio-alsa.c"
#include "lingot-audio-oss.c"
#include "lingot-audio-jack.c"
#include "lingot-audio-pulseaudio.c"
#include "lingot-fft.c"
#include "lingot-core.c"
#include "lingot-signal.c"
#include "lingot-filter.c"

#else

#include "lingot-audio-oss.h"
#include "lingot-audio-alsa.h"
#include "lingot-audio-jack.h"
#include "lingot-audio-pulseaudio.h"

#endif

#include <stdio.h>
#include <string.h>
#include <CUnit/Basic.h>

int main(void) {

#	ifdef OSS
    lingot_audio_oss_register();
#   endif
#	ifdef ALSA
    lingot_audio_alsa_register();
#   endif
#	ifdef PULSEAUDIO
    lingot_audio_pulseaudio_register();
#   endif
#	ifdef JACK
    lingot_audio_jack_register();
#   endif

    CU_pSuite pSuite = NULL;

    /* initialize the CUnit test registry */
    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();

    /* add a suite to the registry */
    pSuite = CU_add_suite("Lingot_test_suite", NULL, NULL);
    if (NULL == pSuite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* add the tests to the suite */
    /* NOTE - ORDER IS IMPORTANT - MUST TEST fread() AFTER fprintf() */
    if ( //
         (NULL == CU_add_test(pSuite, "lingot_config", lingot_test_io_config)) || //
         (NULL == CU_add_test(pSuite, "lingot_config_scale", lingot_test_config_scale)) || //
         (NULL == CU_add_test(pSuite, "lingot_signal", lingot_test_signal)) || //
         (NULL == CU_add_test(pSuite, "lingot_core", lingot_test_core)) || //
         0) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* Run all tests using the CUnit Basic interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    int num_failures = CU_get_number_of_failures();
    CU_cleanup_registry();

    return num_failures;

}
