/*
 * lingot, a musical instrument tuner.
 *
 * Copyright (C) 2004-2011  Ibán Cereijo Graña, Jairo Chapela Martínez.
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

#include "lingot-defs.h"
#include "lingot-audio-pulseaudio.h"
#include "lingot-i18n.h"
#include "lingot-msg.h"

#ifdef PULSEAUDIO
#include <pulse/pulseaudio.h>
#endif

LingotAudioHandler* lingot_audio_pulseaudio_new(char* device, int sample_rate) {

	LingotAudioHandler* audio = NULL;

#	ifdef PULSEAUDIO
	audio = malloc(sizeof(LingotAudioHandler));
	audio->pa_client = 0;
	strcpy(audio->device, "");
	audio->read_buffer = NULL;
	audio->audio_system = AUDIO_SYSTEM_PULSEAUDIO;
	audio->read_buffer_size = 128; // TODO: size up

	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 1;
	ss.rate = sample_rate;

	audio->pa_client = pa_simple_new(NULL, // Use the default server.
			"Lingot", // Our application's name.
			PA_STREAM_RECORD, NULL, // Use the default device.
			"Music", // Description of our stream.
			&ss, // Our sample format.
			NULL, // Use default channel map
			NULL, // Use default buffering attributes.
			NULL // Ignore error code.
			);

#	else
	lingot_msg_add_error(
			_("The application has not been built with PulseAudio support"));
#	endif

	return audio;
}

void lingot_audio_pulseaudio_destroy(LingotAudioHandler* audio) {

#	ifdef PULSEAUDIO
	if (audio != NULL) {
		if (audio->pa_client != 0)
			pa_simple_free(audio->pa_client);
	}
#	endif
}

int lingot_audio_pulseaudio_read(LingotAudioHandler* audio) {

#	ifdef PULSEAUDIO
	int temp_sret;
	int error;

	temp_sret = pa_simple_read(audio->pa_client, audio->read_buffer,
			audio->read_buffer_size, &error);

	if (error != 0) {
		char buff[100];
		sprintf(buff, _("Read from audio interface failed.\n%s."),
				pa_strerror(error));
		lingot_msg_add_error(buff);
		return -1;
	}

	if (temp_sret != audio->read_buffer_size) {
		char buff[100];
		sprintf(buff, _("Read from audio interface failed.\n%s."), "");
		lingot_msg_add_error(buff);
		return -1;
	}

	printf("temp_sret = %i, error = %i\n", temp_sret, error);
#	endif

	return 0;
}

LingotAudioSystemProperties* lingot_audio_pulseaudio_get_audio_system_properties(
		audio_system_t audio_system) {

	LingotAudioSystemProperties* properties = NULL;
#	ifdef PULSEAUDIO
	properties = (LingotAudioSystemProperties*) malloc(
			1 * sizeof(LingotAudioSystemProperties));

	properties->forced_sample_rate = 0;
	properties->n_devices = 0;
	properties->devices = NULL;
	properties->n_sample_rates = 5;
	properties->sample_rates = malloc(properties->n_sample_rates * sizeof(int));
	properties->sample_rates[0] = 8000;
	properties->sample_rates[1] = 11025;
	properties->sample_rates[2] = 22050;
	properties->sample_rates[3] = 44100;
	properties->sample_rates[4] = 48000;

#	endif

	return properties;
}
