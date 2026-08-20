#ifndef DOOM_HIGHBOY_H
#define DOOM_HIGHBOY_H

#ifdef __cplusplus
extern "C" {
#endif

// Launch official DOOM (doomgeneric) in its own FreeRTOS task. Takes over the
// ST7789 (landscape, bypassing LVGL) and streams /sdcard/doom1.wad. Call from
// the games-menu screen open handler. Quitting DOOM reboots the device
// (hold OK+BACK ~2s), so this does not return control to the caller's UI.
//
// render_beat_kick: optional callback pulsed once per frame while DOOM owns the
// panel (LVGL is parked). Pass ui_render_beat_kick so the render-liveness
// watchdog (sys_monitor) sees progress; NULL disables it.
void highboy_doom_start(void (*render_beat_kick)(void));

#ifdef __cplusplus
}
#endif

#endif // DOOM_HIGHBOY_H
