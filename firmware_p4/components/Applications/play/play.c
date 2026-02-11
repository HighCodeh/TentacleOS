#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.h"
#include "play.h"

static const Note pop_music[] = {
    {440, 120}, {0, 30}, {880, 120}, {0, 30},
    {660, 100}, {0, 30}, {990, 100}, {0, 30},
    {550, 100}, {0, 20}, {1100, 130}, {0, 50},
    {880, 100}, {0, 30}, {1320, 150}, {0, 40},
    {330, 80}, {0, 20}, {330, 80}, {0, 20},
    {330, 80}, {0, 40}, {660, 150}, {0, 30},
    {550, 120}, {0, 30}, {880, 80}, {660, 80},
    {990, 80}, {660, 80}, {880, 150}, {0, 50},
    {1100, 100}, {0, 40}, {1320, 200}, {0, 80}
};

void play_pop_music(void) {
    int total_notes = sizeof(pop_music) / sizeof(Note);
    for (int i = 0; i < total_notes; i++) {
        buzzer_play_tone(pop_music[i].freq, pop_music[i].duration_ms);
    }
}

