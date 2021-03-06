/*
 * nas.c -- The Network Audio System backend for the spd_audio library.
 *
 * Copyright (C) 2004,2006 Brailcom, o.p.s.
 *
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1, or (at your option) any later
 * version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this package; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * $Id: nas.c,v 1.8 2006-07-11 16:12:26 hanke Exp $
 */

/* Internal event handler */
void*
_nas_handle_events(void *par)
{
    AudioID *id = par;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    while(1)
	AuHandleEvents(id->aud);   
    
}

/* NAS Server error handler */
/* Unfortunatelly we can't return these errors to the caller
   since this handler gets called in the event handler thread. */
AuBool
_nas_handle_server_error(AuServer *server, AuErrorEvent *event)
{
    fprintf(stderr,"ERROR: Non-fatal server error in NAS\n");

    if (event->type != 0){
	fprintf(stderr, "Event of a different type received in NAS error handler.");
	return -1;
    }

    /* It's a pain but we can't ask for string return code
     since it's not allowed to talk to the server inside error handlers
     because of possible deadlocks. */
    fprintf(stderr,"NAS: Serial number of failed request: %d\n", event->serial);
    fprintf(stderr,"NAS: Error code: %d\n", event->error_code);
    fprintf(stderr,"NAS: Resource id: %d\n", event->resourceid);
    fprintf(stderr,"NAS: Request code: %d\n", event->request_code);
    fprintf(stderr,"NAS: Minor code: %d\n\n", event->minor_code);

    return 0;
}

int
nas_open(AudioID *id, void **pars)
{
    int ret;
    AuBool r;

    if (id == NULL) return -2;

    id->aud = AuOpenServer(pars[0], 0, NULL, 0, NULL, NULL);
    if (!id->aud){
	fprintf(stderr, "Can't connect to NAS audio server\n");
	return -1;
    }

    AuSetErrorHandler(id->aud, _nas_handle_server_error);
    /* return value incompatible with documentation here */
    /*    if (!r){
	fprintf(stderr, "Can't set default NAS event handler\n");
	return -1;
	}*/

    id->flow = 0;

    pthread_cond_init(&id->pt_cond, NULL);
    pthread_mutex_init(&id->pt_mutex, NULL);
    pthread_mutex_init(&id->flow_mutex, NULL);

    ret = pthread_create(&id->nas_event_handler, NULL, _nas_handle_events, (void*) id);
    if(ret != 0){
        fprintf(stderr, "ERROR: NAS Audio module: thread creation failed\n");
        return -2;
    }

    return 0;
}

int
nas_play(AudioID *id, AudioTrack track)
{
    char *buf;
    Sound s;
    AuEventHandlerRec *event_handler;
    int ret;
    float lenght;
    struct timeval now;
    struct timespec timeout;

    if (id == NULL) return -2;
    
    s = SoundCreate(SoundFileFormatNone,
		    AuFormatLinearSigned16LSB,
		    track.num_channels, 
		    track.sample_rate, 
		    track.num_samples, 
		    NULL);

    buf = (char*) track.samples;

    pthread_mutex_lock(&id->flow_mutex);

    event_handler = AuSoundPlayFromData(id->aud, 
			      s,
			      buf,
			      AuNone,
			      ((id->volume + 100)/2) * 1500,
			      NULL, NULL, &id->flow,
			      NULL, NULL, NULL);

    if (event_handler == NULL){
	fprintf (stderr, "AuSoundPlayFromData failed for unknown resons.\n");
	return -1;
    }
    
    if (id->flow == 0){
	fprintf (stderr, "Couldn't start data flow");
    }
    pthread_mutex_unlock(&id->flow_mutex);
    
    /* Another timing magic */
    pthread_mutex_lock(&id->pt_mutex);
    lenght = (((float) track.num_samples) / (float) track.sample_rate);
    gettimeofday(&now, NULL);
    timeout.tv_sec = now.tv_sec + (int) lenght;
    timeout.tv_nsec = now.tv_usec * 1000 + (lenght - (int) lenght) * 1000000000;
    pthread_cond_timedwait(&id->pt_cond, &id->pt_mutex, &timeout);
    pthread_mutex_unlock(&id->pt_mutex);

    pthread_mutex_lock(&id->flow_mutex);
    id->flow = 0;
    pthread_mutex_unlock(&id->flow_mutex);

    return 0;
}

int
nas_stop(AudioID *id)
{
    int ret;

    if (id == NULL) return -2;

    pthread_mutex_lock(&id->flow_mutex);
    if (id->flow != 0)
	AuStopFlow(id->aud, id->flow, NULL);
    id->flow = 0;
    pthread_mutex_unlock(&id->flow_mutex);

    pthread_mutex_lock(&id->pt_mutex);
    pthread_cond_signal(&id->pt_cond);
    pthread_mutex_unlock(&id->pt_mutex);

    return 0;
}

int
nas_close(AudioID *id)
{   
    if (id == NULL) return -2;

    pthread_cancel(id->nas_event_handler);
    pthread_join(id->nas_event_handler, NULL);

    pthread_mutex_destroy(&id->pt_mutex);
    pthread_mutex_destroy(&id->flow_mutex);

    AuCloseServer(id->aud);

    id = NULL;

    return 0;
}

int
nas_set_volume(AudioID*id, int volume)
{
    return 0;
}

/* Provide the NAS backend */
spd_audio_plugin_t nas_functions = {nas_open, nas_play, nas_stop, nas_close, nas_set_volume};
