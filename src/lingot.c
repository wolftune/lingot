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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lingot-audio.h"
#include "lingot-audio-oss.h"
#include "lingot-audio-alsa.h"
#include "lingot-audio-jack.h"
#include "lingot-audio-pulseaudio.h"

#include "lingot-defs.h"
#include "lingot-config.h"
#include "lingot-gui-mainframe.h"
#include "lingot-i18n.h"
#include "lingot-io-config.h"

char CONFIG_FILE_NAME[200];

int main(int argc, char *argv[]) {

#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, LINGOT_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif

    // default config file.
    snprintf(CONFIG_FILE_NAME, sizeof(CONFIG_FILE_NAME),
             "%s/" CONFIG_DIR_NAME DEFAULT_CONFIG_FILE_NAME, getenv("HOME"));

    // TODO: indicate complete config file path
    if ((argc > 3) || (argc == 2)) {
        printf("\nusage: lingot [-c config]\n\n");
        return -1;
    } else if (argc > 1) {
        int c;

        while (1) {
            int option_index = 0;
            struct option long_options[] = { { "config", 1, 0, 'c' }, {0, 0, 0, 0 } };

            c = getopt_long(argc, argv, "c:", long_options, &option_index);
            if (c == -1) {
                break;
            }

            switch (c) {
            case 'c':
                snprintf(CONFIG_FILE_NAME, sizeof(CONFIG_FILE_NAME),
                         "%s/%s%s.conf", getenv("HOME"),
                         CONFIG_DIR_NAME, optarg);
                printf("using config file %s\n", CONFIG_FILE_NAME);
                break;

            case '?':
                break;

            default:
                printf("?? getopt returned character code 0%o ??\n", c);
                break;
            }
        }
    }

    // register audio systems before dealing with the config file
#   if !defined(OSS) && !defined(ALSA) && !defined(JACK) && !defined(PULSEAUDIO)
#	error "No audio system has been defined"
#   endif
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

    lingot_io_config_create_parameter_specs();

    // if config file doesn't exists, i will create it.
    FILE* fp;
    if ((fp = fopen(CONFIG_FILE_NAME, "r")) == NULL) {

        char config_dir[200];
        snprintf(config_dir, sizeof(config_dir), "%s/%s/", getenv("HOME"), CONFIG_DIR_NAME);
        printf("creating directory %s ...\n", config_dir);
        int ret = mkdir(config_dir, 0777); // creo el directorio.
        if (ret) {
            fprintf(stderr, "Cannot create config folder '%s': %s", config_dir, strerror(errno));
        }
        printf("creating file %s ...\n", CONFIG_FILE_NAME);

        // new configuration with default values.
        LingotConfig new_conf;
        lingot_config_new(&new_conf);
        lingot_config_restore_default_values(&new_conf);
        lingot_io_config_save(&new_conf, CONFIG_FILE_NAME);
        lingot_config_destroy(&new_conf);

        printf("ok\n");

    } else {
        fclose(fp);
    }

    lingot_gui_mainframe_create(argc, argv);

    return 0;
}
