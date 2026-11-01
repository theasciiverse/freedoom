/*
 * Asciiverse: sound and music for doom-ascii, which ships without either.
 *
 * One miniaudio playback stream (16-bit stereo) carries everything:
 *
 * - sound effects: the sound module below (sound_av_module) reads Doom's
 *   sound lumps (DMX format: 8-bit unsigned mono at their own rate), and
 *   the stream mixes up to MAX_CHANNELS of them with Doom's volume and
 *   stereo separation, stepping through each at its own rate;
 * - music: Chocolate Doom's OPL music player (i_oplmusic.c, music module
 *   music_opl_module) drives an emulated OPL3 (Nuked OPL3, opl3.c) through
 *   the OPL driver below (opl_av_driver), a port of Chocolate Doom's
 *   opl_sdl.c: the stream asks the emulator for samples and runs the
 *   player's timed callbacks at the right sample.
 *
 * With no sound device (or -nosound) the game plays on silently.
 *
 * Copyright (C) 2005-2014 Simon Howard (opl_sdl.c, the OPL driver's base)
 * Asciiverse changes are in the public domain.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "av_compat.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "opl.h"
#include "opl3.h"
#include "opl_internal.h"
#include "opl_queue.h"

#define MAX_CHANNELS 32

extern int snd_samplerate;

/* ---------------------------------------------------------------------- */
/* The stream                                                              */

static ma_device device;
static int device_users;            /* modules using it: 0, 1 or 2 */
static unsigned int out_rate;
static ma_mutex sfx_mutex;

static void OPL_Mix(int32_t *mix, unsigned int frames);
static void SFX_Mix(int32_t *mix, unsigned int frames);
static int opl_running;
static FILE *dump;     /* AV_AUDIO_DUMP: a copy of the stream, for tests */

static void DataCallback(ma_device *dev, void *output, const void *input,
                         ma_uint32 frames)
{
    static int32_t mix[2 * 4096];
    int16_t *out = output;
    ma_uint32 done = 0;

    (void) dev;
    (void) input;

    while (done < frames)
    {
        unsigned int n = frames - done;
        unsigned int i;

        if (n > 4096)
            n = 4096;
        memset(mix, 0, sizeof(int32_t) * 2 * n);
        SFX_Mix(mix, n);
        if (opl_running)
            OPL_Mix(mix, n);
        for (i = 0; i < 2 * n; i++)
        {
            int32_t v = mix[i];

            out[2 * done + i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        }
        done += n;
    }
    if (dump != NULL)
        fwrite(out, sizeof(int16_t) * 2, frames, dump);
}

static bool StartDevice(void)
{
    ma_device_config config;

    if (device_users++ > 0)
        return true;

    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = snd_samplerate > 0 ? snd_samplerate : 44100;
    config.dataCallback = DataCallback;
    /* about one game tic (28 ms) per period, as Chocolate Doom's slices */
    config.periodSizeInMilliseconds = 28;

    if (ma_mutex_init(&sfx_mutex) != MA_SUCCESS)
    {
        device_users = 0;
        return false;
    }
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS)
    {
        fprintf(stderr, "No sound device: playing without sound.\n");
        ma_mutex_uninit(&sfx_mutex);
        device_users = 0;
        return false;
    }
    out_rate = device.sampleRate;
    /* AV_AUDIO_DUMP=<file>: also write the stream there (raw 16-bit
       stereo), to check the sound without listening */
    if (getenv("AV_AUDIO_DUMP") != NULL && dump == NULL)
        dump = fopen(getenv("AV_AUDIO_DUMP"), "wb");
    if (ma_device_start(&device) != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        ma_mutex_uninit(&sfx_mutex);
        device_users = 0;
        return false;
    }
    return true;
}

static void StopDevice(void)
{
    if (device_users == 0 || --device_users > 0)
        return;
    ma_device_uninit(&device);
    ma_mutex_uninit(&sfx_mutex);
    if (dump != NULL)
    {
        fclose(dump);
        dump = NULL;
    }
}

/* ---------------------------------------------------------------------- */
/* Sound effects                                                           */

typedef struct
{
    int16_t *samples;               /* mono, at the lump's own rate */
    uint32_t length;                /* in samples */
    uint32_t rate;
} av_sfx_t;

typedef struct
{
    const av_sfx_t *sfx;            /* NULL: free */
    uint64_t pos;                   /* 32.32 fixed point, in samples */
    uint64_t step;
    int left, right;                /* 0..255 */
} av_channel_t;

static av_channel_t channels[MAX_CHANNELS];
static bool use_prefix;

static void SFX_Mix(int32_t *mix, unsigned int frames)
{
    int c;

    if (device_users == 0)
        return;
    ma_mutex_lock(&sfx_mutex);
    for (c = 0; c < MAX_CHANNELS; c++)
    {
        av_channel_t *ch = &channels[c];
        unsigned int i;

        if (ch->sfx == NULL)
            continue;
        for (i = 0; i < frames; i++)
        {
            uint32_t at = (uint32_t) (ch->pos >> 32);
            int32_t s;

            if (at >= ch->sfx->length)
            {
                ch->sfx = NULL;
                break;
            }
            /* linear between this sample and the next */
            s = ch->sfx->samples[at];
            if (at + 1 < ch->sfx->length)
            {
                uint32_t frac = (uint32_t) (ch->pos >> 16) & 0xffff;
                s += ((int32_t) (ch->sfx->samples[at + 1] - s) * (int32_t) frac) >> 16;
            }
            mix[2 * i] += (s * ch->left) >> 8;
            mix[2 * i + 1] += (s * ch->right) >> 8;
            ch->pos += ch->step;
        }
    }
    ma_mutex_unlock(&sfx_mutex);
}

/* the lump in DMX format: 3, 0, rate (16 bits), length (32 bits), then
   16 padding bytes, the samples and 16 more padding bytes */
static av_sfx_t *LoadSfx(sfxinfo_t *sfxinfo)
{
    const uint8_t *data;
    uint32_t lumplen, rate, length, i;
    av_sfx_t *sfx;

    if (sfxinfo->lumpnum < 0)
        return NULL;
    data = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);
    lumplen = W_LumpLength(sfxinfo->lumpnum);
    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return NULL;
    }
    rate = data[2] | (data[3] << 8);
    length = data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t) data[7] << 24);
    if (length > lumplen - 8 || length <= 48 || rate == 0)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return NULL;
    }
    /* the DMX padding is not played */
    data += 8 + 16;
    length -= 32;

    sfx = malloc(sizeof *sfx);
    sfx->samples = malloc(sizeof(int16_t) * length);
    sfx->length = length;
    sfx->rate = rate;
    for (i = 0; i < length; i++)
        sfx->samples[i] = (int16_t) ((data[i] - 128) * 256);
    W_ReleaseLumpNum(sfxinfo->lumpnum);
    return sfx;
}

static snddevice_t sound_av_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

static bool AV_InitSound(bool use_sfx_prefix)
{
    use_prefix = use_sfx_prefix;
    memset(channels, 0, sizeof channels);
    return StartDevice();
}

static void AV_ShutdownSound(void)
{
    if (device_users > 0)
    {
        ma_mutex_lock(&sfx_mutex);
        memset(channels, 0, sizeof channels);
        ma_mutex_unlock(&sfx_mutex);
    }
    StopDevice();
}

static int AV_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    if (sfx->link != NULL)
        sfx = sfx->link;
    if (use_prefix)
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    return W_CheckNumForName(namebuf);
}

static void AV_UpdateSound(void)
{
}

/* Doom's volume is 0..127 and separation 0 (left) .. 254 (right) */
static void SetParams(av_channel_t *ch, int vol, int sep)
{
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 254) sep = 254;
    ch->left = ((254 - sep) * vol) / 127;
    ch->right = (sep * vol) / 127;
}

static void AV_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= MAX_CHANNELS || device_users == 0)
        return;
    ma_mutex_lock(&sfx_mutex);
    SetParams(&channels[channel], vol, sep);
    ma_mutex_unlock(&sfx_mutex);
}

static int AV_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    av_sfx_t *sfx;

    if (channel < 0 || channel >= MAX_CHANNELS || device_users == 0)
        return -1;
    if (sfxinfo->driver_data == NULL)
        sfxinfo->driver_data = LoadSfx(sfxinfo);
    sfx = sfxinfo->driver_data;
    if (sfx == NULL)
        return -1;

    ma_mutex_lock(&sfx_mutex);
    channels[channel].sfx = sfx;
    channels[channel].pos = 0;
    channels[channel].step = ((uint64_t) sfx->rate << 32) / out_rate;
    SetParams(&channels[channel], vol, sep);
    ma_mutex_unlock(&sfx_mutex);
    return channel;
}

static void AV_StopSound(int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS || device_users == 0)
        return;
    ma_mutex_lock(&sfx_mutex);
    channels[channel].sfx = NULL;
    ma_mutex_unlock(&sfx_mutex);
}

static bool AV_SoundIsPlaying(int channel)
{
    bool playing;

    if (channel < 0 || channel >= MAX_CHANNELS || device_users == 0)
        return false;
    ma_mutex_lock(&sfx_mutex);
    playing = channels[channel].sfx != NULL;
    ma_mutex_unlock(&sfx_mutex);
    return playing;
}

static void AV_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void) sounds;
    (void) num_sounds;
}

sound_module_t sound_av_module =
{
    sound_av_devices,
    sizeof(sound_av_devices) / sizeof(*sound_av_devices),
    AV_InitSound,
    AV_ShutdownSound,
    AV_GetSfxLumpNum,
    AV_UpdateSound,
    AV_UpdateSoundParams,
    AV_StartSound,
    AV_StopSound,
    AV_SoundIsPlaying,
    AV_CacheSounds,
};

/* ---------------------------------------------------------------------- */
/* Music: the OPL driver, from Chocolate Doom's opl_sdl.c                  */

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

static ma_mutex callback_mutex;
static ma_mutex callback_queue_mutex;
static opl_callback_queue_t *callback_queue;
static uint64_t current_time;
static int opl_paused;
static uint64_t pause_offset;
static opl3_chip opl_chip;
static int opl_opl3mode;
static int16_t *opl_buffer;
static int register_num = 0;
static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

// Advance time by the specified number of samples, invoking any
// callback functions as appropriate.

static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    ma_mutex_lock(&callback_queue_mutex);

    us = ((uint64_t) nsamples * OPL_SECOND) / out_rate;
    current_time += us;

    if (opl_paused)
    {
        pause_offset += us;
    }

    // Are there callbacks to invoke now?  Keep invoking them
    // until there are no more left.

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // The callback runs holding callback_mutex (so that the game can
        // use OPL_Lock() to keep callbacks out) but not
        // callback_queue_mutex, so that it can schedule new callbacks.

        ma_mutex_unlock(&callback_queue_mutex);
        ma_mutex_lock(&callback_mutex);
        callback(callback_data);
        ma_mutex_unlock(&callback_mutex);
        ma_mutex_lock(&callback_queue_mutex);
    }

    ma_mutex_unlock(&callback_queue_mutex);
}

// Called from the stream: add the emulator's output, running the
// callbacks at the samples where they fall due.

static void OPL_Mix(int32_t *mix, unsigned int frames)
{
    unsigned int filled = 0;

    while (filled < frames)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;
        unsigned int i;

        ma_mutex_lock(&callback_queue_mutex);

        if (opl_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = frames - filled;
        }
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next_callback_time - current_time) * out_rate;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > frames - filled)
            {
                nsamples = frames - filled;
            }
        }

        ma_mutex_unlock(&callback_queue_mutex);

        OPL3_GenerateStream(&opl_chip, opl_buffer, (uint32_t) nsamples);
        for (i = 0; i < 2 * nsamples; i++)
        {
            mix[2 * filled + i] += opl_buffer[i];
        }
        filled += nsamples;

        AdvanceTime(nsamples);
    }
}

static void OPL_AV_Shutdown(void)
{
    if (!opl_running)
    {
        return;
    }
    opl_running = 0;
    StopDevice();
    OPL_Queue_Destroy(callback_queue);
    free(opl_buffer);
    opl_buffer = NULL;
    ma_mutex_uninit(&callback_mutex);
    ma_mutex_uninit(&callback_queue_mutex);
}

static int OPL_AV_Init(unsigned int port_base)
{
    (void) port_base;

    if (opl_running)
    {
        return 1;
    }
    if (ma_mutex_init(&callback_mutex) != MA_SUCCESS)
    {
        return 0;
    }
    if (ma_mutex_init(&callback_queue_mutex) != MA_SUCCESS)
    {
        ma_mutex_uninit(&callback_mutex);
        return 0;
    }

    opl_paused = 0;
    pause_offset = 0;
    callback_queue = OPL_Queue_Create();
    current_time = 0;

    if (!StartDevice())
    {
        OPL_Queue_Destroy(callback_queue);
        ma_mutex_uninit(&callback_mutex);
        ma_mutex_uninit(&callback_queue_mutex);
        return 0;
    }

    // Stereo samples for one second, always more than one period.
    opl_buffer = malloc(sizeof(int16_t) * 2 * out_rate);

    OPL3_Reset(&opl_chip, out_rate);
    opl_opl3mode = 0;

    opl_running = 1;
    return 1;
}

static unsigned int OPL_AV_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    // If the timer is enabled, calculate the time when the timer
    // will expire.

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        case OPL_REG_NEW:
            opl_opl3mode = value & 0x01;
            // fall through

        default:
            OPL3_WriteRegBuffered(&opl_chip, reg_num, value);
            break;
    }
}

static void OPL_AV_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_AV_SetCallback(uint64_t us, opl_callback_t callback,
                               void *data)
{
    ma_mutex_lock(&callback_queue_mutex);
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
    ma_mutex_unlock(&callback_queue_mutex);
}

static void OPL_AV_ClearCallbacks(void)
{
    ma_mutex_lock(&callback_queue_mutex);
    OPL_Queue_Clear(callback_queue);
    ma_mutex_unlock(&callback_queue_mutex);
}

static void OPL_AV_Lock(void)
{
    ma_mutex_lock(&callback_mutex);
}

static void OPL_AV_Unlock(void)
{
    ma_mutex_unlock(&callback_mutex);
}

static void OPL_AV_SetPaused(int paused)
{
    opl_paused = paused;
}

static void OPL_AV_AdjustCallbacks(float factor)
{
    ma_mutex_lock(&callback_queue_mutex);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    ma_mutex_unlock(&callback_queue_mutex);
}

opl_driver_t opl_av_driver =
{
    "miniaudio",
    OPL_AV_Init,
    OPL_AV_Shutdown,
    OPL_AV_PortRead,
    OPL_AV_PortWrite,
    OPL_AV_SetCallback,
    OPL_AV_ClearCallbacks,
    OPL_AV_Lock,
    OPL_AV_Unlock,
    OPL_AV_SetPaused,
    OPL_AV_AdjustCallbacks,
};
