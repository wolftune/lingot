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

#ifdef OSS

#include "lingot-audio.h"
#include "lingot-msg.h"
#include "lingot-defs.h"
#include "lingot-audio-oss.h"
#include "lingot-i18n.h"

#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void lingot_audio_oss_new(LingotAudioHandler* audio, const char* device, int sample_rate) {

    int channels = 1;
#	ifdef AFMT_S16_NE
    int format = AFMT_S16_NE;
#	else
    int format = AFMT_S16_LE;
#	endif

    char error_message[512];
    const char* exception;

    audio->audio_handler_extra = malloc(sizeof(LingotAudioHandlerExtraOSS));
    LingotAudioHandlerExtraOSS* audioOSS = (LingotAudioHandlerExtraOSS*) audio->audio_handler_extra;

    strncpy(audio->device, device, sizeof(audio->device) - 1);
    if (!strcmp(audio->device, "default")) {
        strcpy(audio->device, "/dev/dsp");
    }
    audioOSS->dsp = open(audio->device, O_RDONLY);
    if (sample_rate >= 44100) {
        audio->read_buffer_size_samples = 1024;
    } else if (sample_rate >= 22050) {
        audio->read_buffer_size_samples = 512;
    } else {
        audio->read_buffer_size_samples = 256;
    }

    try
    {

        if (audioOSS->dsp < 0) {
            snprintf(error_message, sizeof(error_message),
                     _("Cannot open audio device '%s'.\n%s"), audio->device,
                     strerror(errno));
            throw(error_message);
        }

        //if (ioctl(audio->dsp, SOUND_PCM_READ_CHANNELS, &channels) < 0)
        if (ioctl(audioOSS->dsp, SNDCTL_DSP_CHANNELS, &channels) < 0) {
            snprintf(error_message, sizeof(error_message), "%s\n%s",
                     _("Error setting number of channels."), strerror(errno));
            throw(error_message);
        }

        // sample size
        //if (ioctl(audio->dsp, SOUND_PCM_SETFMT, &format) < 0)
        if (ioctl(audioOSS->dsp, SNDCTL_DSP_SETFMT, &format) < 0) {
            snprintf(error_message, sizeof(error_message), "%s\n%s",
                     _("Error setting bits per sample."), strerror(errno));
            throw(error_message);
        }

        int fragment_size = 1;
        int DMA_buffer_size = 512;
        int param = 0;

        for (param = 0; fragment_size < DMA_buffer_size; param++) {
            fragment_size <<= 1;
        }

        param |= 0x00ff0000;

        if (ioctl(audioOSS->dsp, SNDCTL_DSP_SETFRAGMENT, &param) < 0) {
            snprintf(error_message, sizeof(error_message), "%s\n%s",
                     _("Error setting DMA buffer size."), strerror(errno));
            throw(error_message);
        }

        if (ioctl(audioOSS->dsp, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
            snprintf(error_message, sizeof(error_message), "%s\n%s",
                     _("Error setting sample rate."), strerror(errno));
            throw(error_message);
        }

        audio->real_sample_rate = sample_rate;
        audio->bytes_per_sample = 2;
    }catch {
        lingot_msg_add_error_with_code(exception, errno);
        close(audioOSS->dsp);
        audio->audio_system = -1;
    }
}

void lingot_audio_oss_destroy(LingotAudioHandler* audio) {
    if (audio->audio_system >= 0) {
        LingotAudioHandlerExtraOSS* audioOSS = (LingotAudioHandlerExtraOSS*) audio->audio_handler_extra;
        if (audioOSS->dsp >= 0) {
            close(audioOSS->dsp);
            audioOSS->dsp = -1;
        }
        free(audio->audio_handler_extra);
    }
}

int lingot_audio_oss_read(LingotAudioHandler* audio) {
    int samples_read = -1;
    char buffer [audio->read_buffer_size_samples * audio->bytes_per_sample];

    LingotAudioHandlerExtraOSS* audioOSS = (LingotAudioHandlerExtraOSS*) audio->audio_handler_extra;
    int bytes_read = read(audioOSS->dsp, buffer, sizeof (buffer));

    if (bytes_read < 0) {
        char buff[512];
        snprintf(buff, sizeof(buff), "%s\n%s",
                 _("Read from audio interface failed."), strerror(errno));
        lingot_msg_add_error(buff);
    } else {

        samples_read = bytes_read / audio->bytes_per_sample;
        // float point conversion
        int i;
        const int16_t* read_buffer = (int16_t*) buffer;
        for (i = 0; i < samples_read; i++) {
            audio->flt_read_buffer[i] = read_buffer[i];
        }
    }
    return samples_read;
}

int lingot_audio_oss_get_audio_system_properties(
        LingotAudioSystemProperties* result) {

    result->forced_sample_rate = 0;
    result->n_devices = 1;
    result->devices = (char**) malloc(result->n_devices * sizeof(char*));
    result->devices[0] = strdup("/dev/dsp");

    result->n_sample_rates = 5;
    result->sample_rates[0] = 8000;
    result->sample_rates[1] = 11025;
    result->sample_rates[2] = 22050;
    result->sample_rates[3] = 44100;
    result->sample_rates[4] = 48000;

    return 0;
}

int lingot_audio_oss_register(void)
{
    return lingot_audio_system_register("OSS",
                                        lingot_audio_oss_new,
                                        lingot_audio_oss_destroy,
                                        NULL,
                                        NULL,
                                        NULL,
                                        lingot_audio_oss_read,
                                        lingot_audio_oss_get_audio_system_properties);
}

#endif
