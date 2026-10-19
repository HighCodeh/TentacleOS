#!/usr/bin/env python3
"""Exercise script screen close/delete callbacks during ABI display handoff."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = (ROOT / 'firmware_p4/components/Applications/ui/screens/dev/scripts_ui.c').read_text()
    harness = '''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define SCREEN_APP_RUNNING 1
typedef int lv_event_t;
static void *s_screen=(void *)1, *s_timer, *s_output, *s_status;
static unsigned s_stops;
static uint32_t s_consent_generation;
static bool s_has_display, s_is_app_task, s_modal;
static int s_current_screen;
static void script_runner_stop(void) { ++s_stops; }
static bool script_runner_has_display(void) { return s_has_display; }
static void *tos_app_ctx_current(void) { return s_is_app_task ? (void *)1 : NULL; }
static bool msgbox_is_open(void) { return s_modal; }
static void msgbox_close(void) { s_modal=false; }
static int ui_current_screen(void) { return s_current_screen; }
static void *lv_event_get_target(lv_event_t *event) { (void)event; return (void *)1; }
static void lv_timer_del(void *timer) { assert(timer==(void *)2); }
void ui_scripts_stop(void);
'''
    harness += function(source, 'void ui_scripts_stop(void)') + '\n'
    harness += function(source, 'static void screen_deleted(lv_event_t *event)') + '\n'
    harness += '''int main(void) {
  ui_scripts_stop(); assert(s_stops==1);
  s_has_display=true; s_is_app_task=true;
  ui_scripts_stop(); assert(s_stops==1);
  s_current_screen=SCREEN_APP_RUNNING; s_is_app_task=false; s_timer=(void *)2;
  screen_deleted(NULL); assert(s_stops==1 && s_timer==NULL && s_screen==NULL);
  s_screen=(void *)1; s_current_screen=0;
  ui_scripts_stop(); assert(s_stops==2);
  s_consent_generation=1; s_modal=true;
  screen_deleted(NULL); assert(s_stops==3 && !s_modal && s_consent_generation==0);
  return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='highboy-script-ui-') as tmp:
        directory = Path(tmp)
        (directory / 'test.c').write_text(harness)
        executable = directory / 'test'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(directory / 'test.c'), '-o', str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
    print('PASS: ABI display handoff survives async deletion; navigation cancels scripts and consent')


if __name__ == '__main__':
    main()
