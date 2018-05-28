/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* л smix.c v1.30 ллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

#include "SMIX.H"

//#include <conio.h>
//#include <dos.h>
//#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



//#include "lowmem.h"

#define BUFFER_LENGTH BLOCK_LENGTH*2

#define BYTE unsigned char

#define lo(value) (unsigned char)((value) & 0x00FF)
#define hi(value) (unsigned char)((value) >> 8)

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) > (b)) ? (b) : (a))

#define BLOCK_LENGTH    512   /* Length of digitized sound output block     */
#define VOICES          8     /* Number of available simultaneous voices    */
#define VOLUMES         64    /* Number of volume levels for sound output   */
#define SAMPLING_RATE   22050 /* Sampling rate for output                   */
#define SHIFT_16_BIT    5     /* Bits to shift left for 16-bit output       */

int  init_sb(int baseio, int irq, int dma, int dma16);
void shutdown_sb(void);

void set_sampling_rate(unsigned short rate);

void init_mixing(void);
void shutdown_mixing(void);

int  open_sound_resource_file(const char *filename);
void close_sound_resource_file(void);


int  start_sound(SOUND *sound, int index, unsigned char volume, int loop);
void stop_sound(int index);
int  sound_playing(int index);

void set_sound_volume(unsigned char new_volume);

volatile long intcount;               /* Current count of sound interrupts  */
volatile int  voicecount;             /* Number of voices currently in use  */

short dspversion;
int   autoinit;
int   sixteenbit;
int   smix_sound;

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */


static int resetport;
static int readport;
static int writeport;
static int pollport;
static int ackport;

static int pic_rotateport;
static int pic_maskport;

static int dma_maskport;
static int dma_clrptrport;
static int dma_modeport;
static int dma_addrport;
static int dma_countport;
static int dma_pageport;

static char irq_startmask;
static char irq_stopmask;
static char irq_intvector;

static char dma_startmask;
static char dma_stopmask;
static char dma_mode;
static int  dma_length;

static void (*oldintvector)(void);

static int smix_initialized  = FALSE;
static int handler_installed = FALSE;

static int sampling_rate = SAMPLING_RATE;

void smix_exitproc(void);

int init_sb(int baseio, int irq, int dma, int dma16)
{
    printf("Init SDL\n");
    SDL_AudioSpec desiredSpec;

    desiredSpec.freq = FREQUENCY;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 1;
    desiredSpec.samples = BLOCK_LENGTH;
    desiredSpec.callback = audio_callback;
    desiredSpec.userdata = 0; //this;

    SDL_AudioSpec obtainedSpec;

    // you might want to look for errors here
    SDL_OpenAudio(&desiredSpec, &obtainedSpec);

    smix_initialized = FALSE;
    smix_sound = FALSE;

    atexit(smix_exitproc);

    return(TRUE);
}

void shutdown_sb(void)
{
    SDL_CloseAudio();
}

/* Voice control */

typedef struct
{
    SOUND *sound;
    int   index;
    int   volume;
    int   loop;
    long  curpos;
    int   done;
} VOICE;

static int   inuse[VOICES];
static VOICE voice[VOICES];

static int curblock;

/* Volume lookup table */
static int16_t (*volume_table)[VOLUMES][256];

/* Mixing buffer */
static int16_t mixingblock[BLOCK_LENGTH];  /* Signed 16 bit */

/* Output buffers */
static void          (*outmemarea)                = NULL;
static uint8_t (*out8buf)[2][BLOCK_LENGTH]  = NULL;
static int16_t (*out16buf)[2][BLOCK_LENGTH] = NULL;

static void *blockptr[2];

//static short int outmemarea_sel;              /* Selector for output buffer */

/* Addressing for auto-initialized transfers (Whole buffer)   */
static unsigned long buffer_addr;
static unsigned char buffer_page;
static unsigned int  buffer_ofs;

/* Addressing for single-cycle transfers (One block at a time */
static unsigned long block_addr[2];
static unsigned char block_page[2];
static unsigned int  block_ofs[2];

static unsigned char sound_volume;

/* 8-bit clipping */

static uint8_t (*clip_8_buf)[256*VOICES];
static uint8_t (*clip_8)[256*VOICES];

static char time_constant(int rate)
{
    return (256 - (1000000 / rate));
}

static void init_sampling_rate(unsigned short rate)
{
}

void set_sampling_rate(unsigned short rate)
{
    sampling_rate = rate;

    if (smix_sound)
    {
    }
}

static void start_dac(void)
{
    printf("Play audio\n");
    // start play audio
    SDL_PauseAudio(0);

    smix_sound = TRUE;
}

static void stop_dac(void)
{
    smix_sound = FALSE;

    // stop play audio
    SDL_PauseAudio(1);
}

/* Volume control */

static void init_volume_table(void)
{
    long volume;
    int  insample;

    volume_table = (int16_t (*)[VOLUMES][256]) malloc (VOLUMES * 256*sizeof(int16_t));

    for (volume=0; volume < VOLUMES; volume++)
        for (insample = -128; insample <= 127; insample++)
        {
            (*volume_table)[volume][(unsigned char)insample] =
                (signed int)(((insample*volume) << SHIFT_16_BIT) / (VOLUMES-1));
        }

    sound_volume = 255;
}

void set_sound_volume(unsigned char new_volume)
{
    sound_volume = new_volume;
}

/* Mixing initialization */


void deallocate_voice(int voicenum);

void init_mixing(void)
{
    int i;

    for (i=0; i < VOICES; i++)
        deallocate_voice(i);
    voicecount = 0;

        /* Find a block of memory that does not cross a page boundary */
        outmemarea =
            malloc(sizeof(int16_t)* 2* BLOCK_LENGTH);

        out16buf = (int16_t (*) [2] [BLOCK_LENGTH]) outmemarea;

        for (i=0; i<2; i++)
            blockptr[i] = &((*out16buf)[i]);

        memset(out16buf, 0x00, BUFFER_LENGTH * sizeof(int16_t));

    curblock = 0;
    intcount = 0;

    init_volume_table();
    start_dac();
}

void shutdown_mixing(void)
{
    stop_dac();

    free(volume_table);

    //free(outmemarea_sel);
}

/* Setup for sound resource files */

static int  resource_file = FALSE;
static char resource_filename[64] = "";

int fexist(const char *filename)
{
    FILE *f;

    f = fopen(filename, "r");

    fclose(f);

    return (f != NULL);
}

int  open_sound_resource_file(const char *filename)
{
    resource_file = TRUE;
    strcpy(resource_filename, filename);

    return fexist(filename);
}

void close_sound_resource_file(void)
{
    resource_file = FALSE;
    strcpy(resource_filename, "");
}


/* Loading and freeing sounds */

static FILE *sound_file;
static long sound_size;

typedef struct
{
    char key[8];
    long start;
    long size;
} RESOURCE_HEADER;

void get_sound_file(const char *key)
{
    static short numsounds;
    int   found;
    int   i;
    static RESOURCE_HEADER res_header;

    found = FALSE;
    sound_size = 0;

    if (resource_file)
    {
        sound_file = fopen(resource_filename, "rb");
        fread(&numsounds, sizeof(numsounds), 1, sound_file);

        for (i = 0; i < numsounds; i++)
        {
            fread(&res_header, sizeof(res_header), 1, sound_file);
            if (!strnicmp(key, res_header.key, 8))
            {
                found = TRUE;
                break;
            }
        }

        if (found)
        {
            fseek(sound_file, res_header.start, SEEK_SET);
            sound_size = res_header.size;
        }
    }
    else
    {
        sound_file = fopen(key, "rb");
        fseek(sound_file, 0, SEEK_END);
        sound_size = ftell(sound_file);
        fseek(sound_file, 0, SEEK_SET);
    }
}

/* Loading and freeing sounds */

int load_sound(SOUND **sound, const char *key)
{
    /* Open file and compute size */
    get_sound_file(key);

    if (sound_size == 0)
        return FALSE;

    /* Allocate sound control structure and sound data block */
    (*sound) = (SOUND *) malloc(sizeof(SOUND));
    (*sound)->soundptr  = (signed char *)(malloc(sound_size));
    (*sound)->soundsize = sound_size;

    /* Read sound data and close file (Isn't flat mode nice?) */
    fread((*sound)->soundptr, sizeof(signed char), sound_size, sound_file);
    fclose(sound_file);

    return TRUE;
}

void free_sound(SOUND **sound)
{
    free((*sound)->soundptr);
    free(*sound);
    *sound = NULL;
}

/* Voice maintainance */

void deallocate_voice(int voicenum)
{
    inuse[voicenum] = FALSE;
    voice[voicenum].sound  = NULL;
    voice[voicenum].index  = -1;
    voice[voicenum].volume = 0;
    voice[voicenum].curpos = -1;
    voice[voicenum].loop   = FALSE;
    voice[voicenum].done   = FALSE;
}

int start_sound(SOUND *sound, int index, unsigned char volume, int loop)
{
    int i, voicenum;

    voicenum = -1;
    i = 0;

    do
    {
        if (!inuse[i])
            voicenum = i;
        i++;
    }
    while ((voicenum == -1) && (i < VOICES));

    if (voicenum != -1)
    {
        voice[voicenum].sound  = sound;
        voice[voicenum].index  = index;
        voice[voicenum].volume = volume;
        voice[voicenum].curpos = 0;
        voice[voicenum].loop   = loop;
        voice[voicenum].done   = FALSE;

        inuse[voicenum] = TRUE;
        voicecount++;
    }

    return (voicenum != -1);
}

void stop_sound(int index)
{
    int i;

    for (i=0; i < VOICES; i++)
        if (voice[i].index == index)
        {
            voicecount--;
            deallocate_voice(i);
        }
}

int  sound_playing(int index)
{
    int i;

    /* Search for a sound with the specified index */
    for (i=0; i < VOICES; i++)
        if (voice[i].index == index)
            return(TRUE);

    /* Sound not found */
    return(FALSE);
}

static void update_voices(void)
{
    int voicenum;

    for (voicenum=0; voicenum < VOICES; voicenum++)
    {
        if (inuse[voicenum])
        {
            if (voice[voicenum].done)
            {
                voicecount--;
                deallocate_voice(voicenum);
            }
        }
    }
}

/* Mixing */

static void mix_voice(int voicenum)
{
    SOUND *sound;
    int   mixlength;
    int8_t *sourceptr;
    int16_t *volume_lookup;
    int chunklength;
    int destindex;

    /* Initialization */
    sound = voice[voicenum].sound;

    sourceptr = sound->soundptr + voice[voicenum].curpos;
    destindex = 0;

    /* Compute mix length */
    if (voice[voicenum].loop)
        mixlength = BLOCK_LENGTH;
    else
        mixlength =
            MIN(BLOCK_LENGTH, sound->soundsize - voice[voicenum].curpos);

    volume_lookup =
        (int16_t *)(&(*volume_table)[(uint8_t)((sound_volume*voice[voicenum].volume*VOLUMES) >> 16)]);

    do
    {
        /* Compute the max consecutive samples that can be mixed */
        chunklength =
            MIN(mixlength, sound->soundsize - voice[voicenum].curpos);

        /* Update the current position */
        voice[voicenum].curpos += chunklength;

        /* Update the remaining samples count */
        mixlength -= chunklength;

        /* Mix samples until end of mixing or end of sound data is reached */
        while (chunklength--)
            mixingblock[destindex++] += volume_lookup[(unsigned char)(*(sourceptr++))];

        /* If we've reached the end of the block, wrap to start of sound */
        if (sourceptr == (sound->soundptr + sound->soundsize))
        {
            if (voice[voicenum].loop)
            {
                voice[voicenum].curpos = 0;
                sourceptr = sound->soundptr;
            }
            else
            {
                voice[voicenum].done = TRUE;
            }
        }
    }
    while (mixlength); /* Wrap around to finish mixing if necessary */
}

static void silenceblock(void)
{
    memset(&mixingblock, 0x00, BLOCK_LENGTH*sizeof(int16_t));
}

static void mix_voices(void)
{
    int i;

    silenceblock();

    for (i=0; i < VOICES; i++)
        if (inuse[i])
            mix_voice(i);
}

static void copy_sound16(void)
{
    int i;
    int16_t *destptr;

    destptr   = (int16_t*) blockptr[curblock];

    for (i=0; i < BLOCK_LENGTH; i++)
        destptr[i] = mixingblock[i];
}


static void copy_sound(void)
{
        copy_sound16();
}


void audio_callback(void *_beeper, Uint8 *_stream, int _length)
{
    Sint16 *stream = (Sint16*) _stream;
    int length = _length / 2;
    Beeper* beeper = (Beeper*) _beeper;

    intcount++;

    update_voices();
    mix_voices();

    for (int i=0; i < BLOCK_LENGTH; i++)
        stream[i] = mixingblock[i];

//        copy_sound();
//        curblock = !curblock;  /* Toggle block */

}

void install_handler(void)
{
    handler_installed = TRUE;
}

void uninstall_handler(void)
{
    handler_installed = FALSE;
}

void smix_exitproc(void)
{
    if (smix_initialized)
    {
        stop_dac();
        shutdown_sb();
    }
}

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */
