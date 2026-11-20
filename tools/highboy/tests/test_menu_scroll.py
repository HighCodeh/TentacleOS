#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Exercise the production custom-menu scroll bounds with changing content."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
s = (ROOT / "firmware_p4/components/Applications/ui/components/menu_component/menu_component_ui.c").read_text()
start = s.index("static void position_scroll_bar(")
end = s.index("static void update_selection(", start)
code = s[start:end]
fakes = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>
typedef struct {void *items_cont, *scroll_track, *scroll_bar; int track_y_start, track_h;} menu_component_t;
typedef void lv_obj_t;
typedef struct {int32_t x,y;} lv_point_t;
typedef void (*lv_anim_exec_xcb_t)(void *,int32_t);
typedef struct {int32_t end;} lv_anim_t;
static int32_t offset, extent, thumb, hidden, scroll_end;
#define THUMB_HEIGHT 18
#define SCROLL_ANIM_MS 200
#define lv_anim_path_ease_in_out NULL
#define LV_ANIM_OFF 0
#define LV_ANIM_ON 1
static int item_scrolls;
static bool list_overflows(menu_component_t *m) {(void)m;return extent>0;}
static void update_scroll_state(menu_component_t *m) {(void)m;}
static void lv_obj_scroll_to_view(lv_obj_t *item,int mode) {assert(item && mode==LV_ANIM_ON);item_scrolls++;}
#define LV_OBJ_FLAG_HIDDEN 1
static void lv_obj_update_layout(void *p) {(void)p;}
static int32_t lv_obj_get_scroll_top(void *p) {(void)p;return offset;}
static int32_t lv_obj_get_scroll_bottom(void *p) {(void)p;return extent-offset;}
static int32_t lv_obj_get_scroll_y(void *p) {(void)p;return offset;}
static void lv_obj_scroll_to_y(void *p,int32_t y,int anim) {(void)p;(void)anim;offset=y;}
static int32_t lv_obj_get_height(void *p) {(void)p;return THUMB_HEIGHT;}
static int32_t lv_obj_get_y(void *p) {(void)p;return thumb;}
static void lv_obj_set_y(void *p,int32_t y) {(void)p;thumb=y;}
static void lv_obj_get_scroll_end(void *p,lv_point_t *end) {(void)p;end->y=scroll_end;}
static void lv_anim_init(lv_anim_t *a) {a->end=0;}
static void lv_anim_set_var(lv_anim_t *a,void *p) {(void)a;(void)p;}
static void lv_anim_set_values(lv_anim_t *a,int32_t from,int32_t to) {(void)from;a->end=to;}
static void lv_anim_set_duration(lv_anim_t *a,int ms) {(void)a;assert(ms==SCROLL_ANIM_MS);}
static void lv_anim_set_path_cb(lv_anim_t *a,void *p) {(void)a;(void)p;}
static void lv_anim_set_exec_cb(lv_anim_t *a,lv_anim_exec_xcb_t cb) {(void)a;(void)cb;}
static void lv_anim_start(lv_anim_t *a) {thumb=a->end;}
static void lv_obj_add_flag(void *p,int f) {(void)p;(void)f;hidden=1;}
static void lv_obj_remove_flag(void *p,int f) {(void)p;(void)f;hidden=0;}
"""
tests = r"""
int main(void) {
 menu_component_t m={.items_cont=(void *)1,.scroll_bar=(void *)2,.track_h=143};
 for(int i=0;i<100;i++) menu_component_scroll_content(&m,60);
 assert(offset==0 && hidden);
 extent=125;
 for(int i=0;i<100;i++) menu_component_scroll_content(&m,60);
 assert(offset==125 && thumb==125 && !hidden);
 for(int i=0;i<100;i++) menu_component_scroll_content(&m,-60);
 assert(offset==0 && thumb==0);
 menu_component_scroll_content(&m,INT_MAX);assert(offset==125);
 extent=20;menu_component_scroll_content(&m,0);assert(offset==20);
 extent=-10;menu_component_scroll_content(&m,0);assert(offset==0 && hidden);
 menu_component_scroll_content(&m,INT_MIN);assert(offset==0);
 menu_component_scroll_content(NULL,60);
 m.track_y_start=10;m.track_h=118;
 position_scroll_bar(&m,-100,200);assert(thumb==10);
 position_scroll_bar(&m,50,200);assert(thumb==35);
 position_scroll_bar(&m,300,200);assert(thumb==110);
 position_scroll_bar(&m,40,-1);assert(thumb==10);
 extent=200;offset=50;scroll_end=200;update_scroll_bar(&m);assert(thumb==110);
 scroll_end=0;update_scroll_bar(&m);assert(thumb==10);
 m.track_h=0;position_scroll_bar(&m,200,200);assert(thumb==10);
 update_scroll_bar(NULL);
 extent=0;menu_component_scroll_to_item(&m,(void *)3);assert(item_scrolls==0);
 extent=200;menu_component_scroll_to_item(&m,(void *)3);assert(item_scrolls==1);
 menu_component_scroll_to_item(NULL,(void *)3);
 menu_component_scroll_to_item(&m,NULL);assert(item_scrolls==1);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="menu-scroll-") as tmp:
    path=Path(tmp); c=path/"test.c"; binary=path/"test"
    c.write_text(fakes+code+tests)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined",str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("PASS: empty/short lists, scrolling bounds, box position, live scroll destination and content shrink")
