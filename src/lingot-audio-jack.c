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

#ifdef JACK

#include <stdio.h>

#include "lingot-audio.h"
#include "lingot-defs.h"
#include "lingot-audio-jack.h"
#include "lingot-i18n.h"
#include "lingot-msg.h"

#include <jack/jack.h>
#include <stdlib.h>
#include <memory.h>

// persistent JACK client to obtain hardware parameters
static jack_client_t* client = NULL;
static pthread_mutex_t stop_mutex = PTHREAD_MUTEX_INITIALIZER;

// this array allows us to reconnect the client to the last ports it was
// connected in a previous session
#define MAX_LAST_PORTS 10
static char last_ports[MAX_LAST_PORTS][80];

// JACK calls this shutdown_callback if the server ever shuts down or
// decides to disconnect the client.
void lingot_audio_jack_shutdown(void* param) {
    LingotAudioHandler* audio = param;
    lingot_msg_add_error(_("Missing connection with JACK audio server"));
    pthread_mutex_lock(&stop_mutex);
    audio->interrupted = 1;
    pthread_mutex_unlock(&stop_mutex);
}

void lingot_audio_jack_new(LingotAudioHandler* audio, const char* device, int sample_rate) {
    (void) sample_rate; // unused
    const char* exception;
    //	const char **ports = NULL;
    const char *client_name = "lingot";
    const char *server_name = NULL;

    jack_options_t options = JackNoStartServer;
    jack_status_t status;

    strcpy(audio->device, "");

    audio->bytes_per_sample = -1;
    audio->audio_handler_extra = malloc(sizeof(LingotAudioHandlerExtraJack));
    LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;

    audioJack->client = jack_client_open(client_name, options, &status,
                                         server_name);

    try
    {
        if (audioJack->client == NULL) {
            throw(_("Unable to connect to JACK server"));
        }

        if (status & JackServerStarted) {
            fprintf(stderr, "JACK server started\n");
        }
        if (status & JackNameNotUnique) {
            client_name = jack_get_client_name(audioJack->client);
            fprintf(stderr, "unique name `%s' assigned\n", client_name);
        }

        jack_on_shutdown(audioJack->client, lingot_audio_jack_shutdown, audio);

        audio->real_sample_rate = jack_get_sample_rate(audioJack->client);
        audio->read_buffer_size_samples = jack_get_buffer_size(
                    audioJack->client);

        //	printf("engine sample rate: %" PRIu32 "\n", jack_get_sample_rate(
        //			audio->jack_client));
        //	printf("buffer size: %" PRIu32 "\n", jack_get_buffer_size(
        //			audio->jack_client));

        audioJack->input_port = jack_port_register(audioJack->client, "input",
                                                   JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

        if (audioJack->input_port == NULL) {
            throw(_("No more JACK ports available"));
        }

        snprintf(audio->device, sizeof(audio->device), "%s", device);

    }catch {
        audio->audio_system = -1;
        lingot_msg_add_error(exception);
    }

    if (audio->audio_system >= 0) {
        client = audioJack->client;
    }
}

void lingot_audio_jack_destroy(LingotAudioHandler* audio) {
    if (audio->audio_system >= 0) {
        LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;
        //jack_cycle_wait(audio->jack_client);
        //		jack_deactivate(audio->jack_client);
        jack_client_close(audioJack->client);
        client = NULL;
        free(audio->audio_handler_extra);
    }
}

int lingot_audio_jack_read(LingotAudioHandler* audio) {
    register int i;
    LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;
    float* in = jack_port_get_buffer(audioJack->input_port, audioJack->nframes);
    for (i = 0; i < audioJack->nframes; i++) {
        audio->flt_read_buffer[i] = in[i] * FLT_SAMPLE_SCALE;
    }
    return 0;
}

int lingot_audio_jack_process(jack_nframes_t nframes, void* param) {
    LingotAudioHandler* audio = param;
    LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;
    audioJack->nframes = nframes;

    pthread_mutex_lock(&stop_mutex);
    if (audio->running) {
        lingot_audio_jack_read(audio);
        audio->process_callback(audio->flt_read_buffer,
                                audio->read_buffer_size_samples, audio->process_callback_arg);
    }
    pthread_mutex_unlock(&stop_mutex);

    return 0;
}

int lingot_audio_jack_get_audio_system_properties(
        LingotAudioSystemProperties* properties) {
    int sample_rate = -1;

    const char *client_name = "lingot-get-audio-properties";
    const char *server_name = NULL;

    jack_options_t options = JackNoStartServer;
    jack_status_t status;
    jack_client_t* jack_client = NULL;
    const char **ports = NULL;
    const char* exception;

    unsigned long int flags = JackPortIsOutput;

    properties->forced_sample_rate = 1;
    properties->n_devices = 0;
    properties->devices = NULL;

    try
    {
        if (client == NULL) {
            jack_client = jack_client_open(client_name, options, &status,
                                           server_name);
            if (jack_client == NULL) {
                throw(_("Unable to connect to JACK server"));
            }
            if (status & JackServerStarted) {
                fprintf(stderr, "JACK server started\n");
            }
            if (status & JackNameNotUnique) {
                client_name = jack_get_client_name(jack_client);
                fprintf(stderr, "unique name `%s' assigned\n", client_name);
            }
        } else {
            sample_rate = jack_get_sample_rate(client);
            ports = jack_get_ports(client, NULL, NULL, flags);
        }

    }catch {
        // here I throw a warning message because we are only ontaining the
        // audio properties
        //		lingot_msg_add_warning(exception);
        fprintf(stderr, "%s", exception);
    }

    properties->forced_sample_rate = 1;
    properties->n_devices = 1;
    if (ports != NULL) {
        int i;
        for (i = 0; ports[i] != NULL; i++) {
        }
        properties->n_devices = i + 1;
    }

    properties->devices = (char**) malloc(
                properties->n_devices * sizeof(char*));
    char buff[512];
    snprintf(buff, sizeof(buff), "%s <default>", _("Default Port"));
    properties->devices[0] = strdup(buff);

    if (ports != NULL) {
        if (properties->n_devices != 0) {
            int i;
            for (i = 0; ports[i] != NULL; i++) {
                properties->devices[i + 1] = strdup(ports[i]);
            }
        }
    }

    if (sample_rate == -1) {
        properties->n_sample_rates = 0;
    } else {
        properties->n_sample_rates = 1;
        properties->sample_rates[0] = sample_rate;
    }

    if (ports != NULL) {
        jack_free(ports);
    }

    if (jack_client != NULL) {
        jack_client_close(jack_client);
    }

    return 0;
}

void lingot_audio_jack_stop(LingotAudioHandler* audio) {
    LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;
    //jack_cycle_wait(audioJack->jack_client);
    const char** ports = jack_get_ports(audioJack->client, NULL, NULL,
                                        JackPortIsOutput);

    if (ports != NULL) {
        int i, j = 0;

        for (i = 0; i < MAX_LAST_PORTS; i++) {
            strcpy(last_ports[i], "");
        }

        for (i = 0; ports[i]; i++) {
            if (jack_port_connected(
                        jack_port_by_name(audioJack->client, ports[i]))) {
                strcpy(last_ports[j++], ports[i]);
            }
        }
    }

    pthread_mutex_lock(&stop_mutex);
    jack_deactivate(audioJack->client);
    pthread_mutex_unlock(&stop_mutex);
}

int lingot_audio_jack_start(LingotAudioHandler* audio) {
    int result = 0;
    int index = 0;
    const char **ports = NULL;
    const char* exception;
    LingotAudioHandlerExtraJack* audioJack = (LingotAudioHandlerExtraJack*) audio->audio_handler_extra;
    jack_set_process_callback(audioJack->client, lingot_audio_jack_process,
                              audio);

    try
    {
        if (jack_activate(audioJack->client)) {
            throw(_("Cannot activate client"));
        }

        ports = jack_get_ports(audioJack->client, NULL, NULL,
                               JackPortIsOutput);
        if (ports == NULL) {
            throw(_("No active capture ports"));
        }

        if (!strcmp(audio->device, "default")) {
            // try to connect the client to the ports is was connected before
            int j = 0;
            int connections = 0;
            for (j = 0; j < MAX_LAST_PORTS; j++) {
                for (index = 0; ports[index]; index++) {
                    if (!strcmp(last_ports[j], ports[index])) {
                        if (jack_connect(audioJack->client, ports[index],
                                         jack_port_name(audioJack->input_port))) {
                            throw(_("Cannot connect input ports"));
                        } else {
                            connections++;
                        }
                    }
                }
            }

            // if there wasn't connections before, we connect the client to the
            // first physical port
            if (!connections) {
                if (jack_connect(audioJack->client, ports[0],
                                 jack_port_name(audioJack->input_port))) {
                    throw(_("Cannot connect input ports"));
                }
            }
        } else {
            if (jack_connect(audioJack->client, audio->device,
                             jack_port_name(audioJack->input_port))) {
                char buff[512];
                snprintf(buff, sizeof(buff),
                         _("Cannot connect to requested port '%s'"),
                         audio->device);
                throw(buff);
            }
        }
    }catch {
        lingot_msg_add_error(exception);
        result = -1;

        lingot_audio_jack_stop(audio);
    }

    if (ports != NULL) {
        jack_free(ports);
    }

    return result;
}


int lingot_audio_jack_register(void)
{
    return lingot_audio_system_register("JACK",
                                        lingot_audio_jack_new,
                                        lingot_audio_jack_destroy,
                                        lingot_audio_jack_start, // self-driven
                                        lingot_audio_jack_stop,
                                        NULL,
                                        NULL,
                                        lingot_audio_jack_get_audio_system_properties);
}

#endif
