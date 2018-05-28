/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* mixtest.c */

//#include <conio.h>
//#include <graph.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//#include "detect.h"
#include "smix.h"

#define ON  1
#define OFF 0

#define TRUE  1
#define FALSE 0

#define NUMSOUNDS 6

#define random(num) (int)(((long)rand()*(num))/(RAND_MAX+1))
#define randomize() srand((unsigned)time(NULL))

const char *resource_file = "mixtest.snd";

const char *sound_key[NUMSOUNDS] =
{
    "JET",
    "GUN",
    "CRASH",
    "CANNON",
    "LASER",
    "GLASS"
};

int baseio, irq, dma, dma16;

SOUND *sound[NUMSOUNDS];

void ourexitproc(void)
{
    int i;

    for (i=0; i < NUMSOUNDS; ++i)
        if (sound[i] != NULL)
            free_sound(sound+i);
}

void loadsounds(void)
{
    int i;

    printf("Loading sounds\n");

    if (!open_sound_resource_file(resource_file))
    {
        printf("ERROR:  Can't load sound resource file\n");
        exit(EXIT_FAILURE);
    }

    for (i=0; i < NUMSOUNDS; i++)
        load_sound(&(sound[i]), sound_key[i]);

    close_sound_resource_file();

    atexit(ourexitproc);
}

void freesounds(void)
{
    int i;

    for (i = 0; i < NUMSOUNDS; i++)
        free_sound(sound+i);
}

void init(void)
{
    printf("-------------------------------------------\n");
    printf("Sound Mixing Library v1.30 by Ethan Brodsky\n");
    {
        if (!init_sb(baseio, irq, dma, dma16))
        {
            printf("ERROR:  Error initializing sound card!\n");
            exit(EXIT_FAILURE);
        }
    }

    printf("BaseIO=%Xh     IRQ%u     DMA8=%u     DMA16=%u\n", baseio, irq, dma, dma16);

    printf("DSP version %.1u.%.02u:  ", dspversion>>8, dspversion&0xFF);
    if (sixteenbit)
        printf("16-bit, ");
    else
        printf("8-bit, ");
    if (autoinit)
        printf("Auto-initialized\n");
    else
        printf("Single-cycle\n");

    init_mixing();

    printf("\n");
}

void shutdown(void)
{
    shutdown_mixing();
    shutdown_sb();
}

int main(int argc, char *argv[])
{
    SDL_Init(SDL_INIT_AUDIO);
/*
    int duration = 1000;
    double Hz = 440;

    Beeper b;
    b.beep(Hz, duration);
    b.wait();
*/
    int  jet        = 0;
    int  randsound  = 1;
    int  rate       = 22000;
    int  volume     = 255;

//    struct rccoord coords;
    int  stop;
    long counter;
    char inkey;
    int  num;

    randomize();

    init();

    loadsounds();

    printf("Press:                       \n");
    printf("  J   Toggle jet engine      \n");
    printf("  R   Toggle random sounds   \n");
    printf("  1   Machine Gun            \n");
    printf("  2   Crash                  \n");
    printf("  3   Cannon                 \n");
    printf("  4   Laser                  \n");
    printf("  5   Breaking glass         \n");
    printf("  -   Decrease volume        \n");
    printf("  +   Increase volume        \n");
    printf("  <   Reduce sampling rate   \n");
    printf("  >   Increase sampling rate \n");
    printf("  Q   Quit                   \n");

    set_sound_volume(volume);

//    coords = _gettextposition();

    stop = FALSE;
    counter = 0;

    do
    {
        /* Increment and display counters */
        counter++;
        printf("%8lu %8lu %4u %8u %8u\n", counter, intcount, voicecount, volume, rate);
//       _settextposition(coords.row-1, 1);

        /* Maybe start a random sound */
        if (randsound && (random(2500) == 0))
        {
            num = (random(NUMSOUNDS-1))+1;
            start_sound(sound[num], num, 64+random(192), OFF);
        }

        /* Start a sound if a key is pressed */
        //if (kbhit())
        {
            inkey = getchar();
            if ((inkey >= '0') && (inkey <= '9'))
            {
                num = inkey - '0'; /* Convert to integer */
                if (num < NUMSOUNDS)
                {
                    printf("Sound:%d\n",num);
                    start_sound(sound[num], num, 255, OFF);
                }

            }
            else
            {
                switch(inkey)
                {
                case 'j':
                case 'J':
                    jet = !jet;
                    if (jet)
                        start_sound(sound[0], 0, 255, ON);
                    else
                        stop_sound(0);
                    break;

                case 'R':
                case 'r':
                    randsound = !randsound;
                    break;

                case '-':
                case '_':
                    volume -= 4;
                    if (volume < 0)
                        volume = 0;
                    set_sound_volume(volume);
                    break;

                case '+':
                case '=':
                    volume += 4;
                    if (volume > 255)
                        volume = 255;
                    set_sound_volume(volume);
                    break;

                case '<':
                case ',':
                    rate -= 250;
                    if (rate < 5000)
                        rate = 5000;
                    set_sampling_rate(rate);
                    break;

                case '>':
                case '.':
                    rate += 250;
                    if (rate > 48000)
                        rate = 48000;
                    set_sampling_rate(rate);
                    break;
                case '\n':
                    break;

                default:
                    stop = TRUE;
                    break;
                }
            }
        }
    }
    while (!stop);

    if (jet)
        stop_sound(0);

    shutdown();

    freesounds();

    printf("\n");

    return(0);
}
