// license:MIT License
// copyright-holders:Hiromasa Tanaka
#include <stdio.h>
#include <stdlib.h>

#include <msx/gfx.h>
#include "psgdriver.h"

#define MSX_CLIKSW  0xf3db
#define MSX_JIFFY   0xfc9e
#define MSX_H_TIMI  0xfd9f

#define VRAM_NONE   0x20
#define VRAM_START  0x1800
#define VRAM_WIDTH  32
#define VRAM_HEIGHT 24
#define VPOS(x, y)  (VRAM_START + VRAM_WIDTH * y + x)

// chars.asm::_chars ラベルの参照 (For VRAM PCG transfers)
extern unsigned char chars[];
// 音楽・Sound effect reference table（psgdriver.asm 用）
extern uint8_t music_title[], music_main[], music_game_over[], sound_extend[], sound_get[];
// Sound Status (from work area) 
extern uint8_t sounddrv_bgmwk[];

// game state translation
typedef enum {
    TITLE_INIT,
    TITLE_ADVERTISE,
    GAME_INIT,
    GAME_MAIN,
    GAME_OVER
} game_state_t;

// game state
typedef struct {
    game_state_t state;
    uint8_t remein_clear;
    uint16_t score;
    uint16_t score_hi;
    uint8_t stick_state;
    uint8_t sound_play;
} game_t;

game_t game;

// ball state
typedef struct {
    uint8_t x;
    uint8_t y;
    int8_t vy;
    uint16_t tick;
} ball_t;

ball_t ball;

// state of advertising demo
typedef struct {
    uint16_t y;
    int16_t vy;
    uint16_t tick;
    uint8_t trigger_state;
} title_t;

title_t title;

/**
 * wait for vsync count
 */
void wait_vsync(uint16_t count)
{
    uint16_t *interval_timer = (uint16_t *)MSX_JIFFY;
    *interval_timer = 0;
    while(*interval_timer < count);
}

/**
 * graphics initialization
 */
void init_graphics()
{
    // screen mode set
    set_color(15, 1, 1);
    set_mangled_mode();

    // sprite mode
    set_sprite_mode(sprite_default);

    // key click switch (OFF)
    *(uint8_t *)MSX_CLIKSW = 0;

    // clear screen
    fill(VRAM_START, VRAM_NONE, VRAM_WIDTH * VRAM_HEIGHT);

    // PCG 設定 (transfer same data 3 planes)
    vwrite(chars, 0x0000, 0x800);
    vwrite(chars, 0x0800, 0x800);
    vwrite(chars, 0x1000, 0x800);

    // color setting (0000|0000 = front | background)
    set_char_color('=', 0x54, place_all);
    set_char_color('$', 0xa0, place_all);
    set_char_color('>', 0x6d, place_all);
    set_char_color('?', 0x60, place_all);
}

/**
 * Display score etc.
 */
void print_state()
{
    uchar score_string[VRAM_WIDTH + 1];
    sprintf(score_string, "SCORE %06u  HISCORE %06u  %02u", game.score, game.score_hi, game.remein_clear);
    vwrite(score_string, VPOS(0, 0), VRAM_WIDTH);
}

/**
 * initialize title display and advertising demo
 */
void title_init()
{
    // screen clear
    fill(VRAM_START, VRAM_NONE, VRAM_WIDTH * VRAM_HEIGHT);

    game.remein_clear = 0;
    print_state();

    vwrite("========   =======  ==     ==   ", VPOS(0,  5), VRAM_WIDTH);
    vwrite("==     == ==     == ===    ==   ", VPOS(0,  6), VRAM_WIDTH);
    vwrite("==     == ==     == ====   ==   ", VPOS(0,  7), VRAM_WIDTH);
    vwrite("========  ==     == ==  == ==   ", VPOS(0,  8), VRAM_WIDTH);
    vwrite("==        ==     == ==   ====   ", VPOS(0,  9), VRAM_WIDTH);
    vwrite("==         =======  ==     ==   ", VPOS(0, 10), VRAM_WIDTH);
    vwrite("                                ", VPOS(0, 11), VRAM_WIDTH);
    vwrite("   ========   =======  ==     ==", VPOS(0, 12), VRAM_WIDTH);
    vwrite("   ==     == ==     == ===    ==", VPOS(0, 13), VRAM_WIDTH);
    vwrite("   ==     == ==     == ====   ==", VPOS(0, 14), VRAM_WIDTH);
    vwrite("   ========  ==     == ==  == ==", VPOS(0, 15), VRAM_WIDTH);
    vwrite("   ==        ==     == ==   ====", VPOS(0, 16), VRAM_WIDTH);
    vwrite("   ==         =======  ==     ==", VPOS(0, 17), VRAM_WIDTH);
    vwrite("          HIT SPACE KEY         ", VPOS(0, 22), VRAM_WIDTH);

    title.y = 6;
    title.tick = 0;
    title.vy = 1;

    // play sound
    sounddrv_bgmplay(music_title);

    // transition to advertising demo
    game.state = TITLE_ADVERTISE;
}

/**
 * title advertising demo
 */
void title_advertise(uint8_t trigger)
{
    // input ahead between frames
    // No input for the first 30 ticks
    // due to ignoring continous input after game over

    if(title.tick > 30 && trigger) {
        title.trigger_state = trigger;
    } else {
        title.trigger_state = 0;
    }

    // HIT SPACE KEY
    if(title.trigger_state) {
        // Progress of the advertising demo
        // initialize the random number generator with
        // the number of ticks inside the advertising demo
        
        seed_rnd(title.tick);
        // stop sound
        sounddrv_stop();
        // transition to game initialization
        game.state = GAME_INIT;
    }

    // tick per 6
    if(title.tick++ % 6 != 0) return;

    // pom pom
    vpoke(VPOS(14, title.y), VRAM_NONE);
    if(title.y >= 9) title.vy = -1;
    if(title.y <= 6) title.vy = 1;
    title.y += title.vy;
    vpoke(VPOS(14, title.y), '?');
}

/**
 * Game bombs and gold distribution
 */
void game_randam_block(uint8_t count)
{
    // Number until clear
    game.remein_clear = 10;
    print_state();

    // Gold and obsticals
    for(uint8_t i = 0; i < count; i++) {
        uint8_t x = get_rnd() % 31 + 1;
        uint8_t y = get_rnd() % 21 + 1;
        uint16_t add = VPOS(x, y);
        if(vpeek(add) == VRAM_NONE || vpeek(add) == '>') {
            if(i % 3) {
                // Gold can crush bombs
                vpoke(add, '$');
            } else {
                // Don't place bombs on your vertical direction
                if(ball.x != x) {
                    vpoke(add, '>');
                }
            }
        }
    }
}

/**
 * game initialization
 */
void game_init()
{
    // clear screen
    fill(VRAM_START, VRAM_NONE, VRAM_WIDTH * VRAM_HEIGHT);

    // wall
    fill(VPOS(0,  1) , '=', VRAM_WIDTH);
    fill(VPOS(0, 22), '=', VRAM_WIDTH);
    for(uint8_t y = 2; y < 22; y++) {
        vpoke(VPOS( 0, y), '=');
        vpoke(VPOS(31, y), '=');
        if(y == 7 || y == 16) {
            vwrite("=======      ======      =======", VPOS(0, y), 32);
        }
    }

    // ball (bottom to top)
    ball.x = 2;
    ball.y = 6;
    ball.vy = -1;

    // initial bomb and gold distribution
    game_randam_block(40);

    // clear state
    ball.tick = 0;
    game.score = 0;
    game.stick_state = 0;

    // display state
    print_state();

    // sound status initializatoin
    game.sound_play = 0;

    // transition to game
    game.state = GAME_MAIN;
}

/**
 * Game Over
 */
void game_over(uint8_t trigger)
{
    vwrite("=                              =", VPOS(0,  9), VRAM_WIDTH);
    vwrite("=          GAME OVER           =", VPOS(0, 10), VRAM_WIDTH);
    vwrite("=                              =", VPOS(0, 11), VRAM_WIDTH);
    vwrite("=        HIT SPACE KEY         =", VPOS(0, 12), VRAM_WIDTH);
    vwrite("=                              =", VPOS(0, 13), VRAM_WIDTH);

    // HIT SPACE KEY
    if(trigger) {
        // transition to title
        game.state = TITLE_INIT;
    }
}

/**
 * Game main
 */
void game_main(uint8_t stick)
{
    uint8_t ball_next_x;
    uint8_t ball_next_y;
    int8_t ball_vx = 0;

    // input ahead between frames
    if(stick & st_left || stick & st_right) {
        game.stick_state = stick;
    }

    // fanfare playback and detection
    if(game.sound_play == 2 && (sounddrv_bgmwk[3] + sounddrv_bgmwk[4]) == 0) {
        game.sound_play = 0;
    }
    // play main music
    if(game.sound_play == 0) {
        sounddrv_bgmplay(music_main);
        // main music playing
        game.sound_play = 1;
    }

    // tick per 6
    if(ball.tick++ % 6 != 0) return;

    // ball destination calculation
    ball_next_y = ball.y + ball.vy;
    if(ball.x > 1 && game.stick_state & st_left) ball_vx = -1;
    if(ball.x < 30 && game.stick_state & st_right) ball_vx += 1;
    ball_next_x = ball.x + ball_vx;

    // Judgement
    uint8_t next_block = vpeek(VPOS(ball_next_x, ball_next_y));
    if(next_block == '=') {
        // wall reflection (stops ball-like 1 tick Stop vertical movement destruction grace period)
        ball_next_y = ball_next_y - ball.vy;
        ball.vy = ball.vy * -1;
    } else if(next_block == '$') {
        // play sound effect
        sounddrv_sfxplay(sound_get);
        // get gold
        game.score += 10;
        if(game.score > game.score_hi) {
            game.score_hi = game.score;
        }
        game.remein_clear--;
        print_state();
        // next stage
        if(game.remein_clear <= 0) {
            // playing fanfare
            game.sound_play = 2;
            // play fanfare
            sounddrv_bgmplay(sound_extend);
            // add bomb and gold
            game_randam_block(20);
        }
    } else if(next_block == '>') {
        // bomb clash
        // stop sound
        sounddrv_stop();
        // play sound
        sounddrv_bgmplay(music_game_over);
        // transition to game over
        game.state = GAME_OVER;
    }

    // ball clear
    vpoke(VPOS(ball.x, ball.y), VRAM_NONE);
    // ball display
    vpoke(VPOS(ball_next_x, ball_next_y), '?');

    // update
    ball.x = ball_next_x;
    ball.y = ball_next_y;

    // clear input ahead between frames
    game.stick_state = 0;
}

/**
 * game loop and state transition
 */
void loop()
{
    while(1) {
        // vsync wait
        wait_vsync(1);
        // get input
        uint8_t stick = st_dir[get_stick(0)];
        uint8_t trigger = get_trigger(0);
        // game
        switch(game.state) {
            case TITLE_INIT:
                title_init();
                break;
            case TITLE_ADVERTISE:
                title_advertise(trigger);
                break;
            case GAME_INIT:
                game_init();
                break;
            case GAME_MAIN:
                game_main(stick);
                break;
            case GAME_OVER:
                game_over(trigger);
                break;
            default:
                break;
        }
    }
}

/**
 * Main
 */
void main()
{
    // screen initialization
    init_graphics();

    // initialize sound driver
    sounddrv_init();
    // sound driver hook settings
    // __INTELLISENSE__ prevent error from non-standard assembly language in vscode

#ifndef __INTELLISENSE__
    __asm
    DI
    __endasm;
    #endif
    uint8_t *h_time = (uint8_t *)MSX_H_TIMI;
    uint16_t hook = (uint16_t)sounddrv_exec;
    h_time[0] = 0xc3; // JP
    h_time[1] = (uint8_t)(hook & 0xff);
    h_time[2] = (uint8_t)((hook & 0xff00) >> 8);
    #ifndef __INTELLISENSE__
    __asm
    EI
    __endasm;
    #endif

    // initialize game state
    game.state = TITLE_INIT;
    game.score_hi = 300;

    // start
    loop();
}
