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
start = s.index("void menu_component_scroll_content(")
end = s.index("static void update_selection(", start)
code = s[start:end]
fakes = r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>
typedef struct {void *items_cont, *scroll_track, *scroll_bar;} menu_component_t;
static int32_t offset, extent, thumb, hidden;
#define LV_ANIM_OFF 0
#define LV_OBJ_FLAG_HIDDEN 1
static void lv_obj_update_layout(void *p) {(void)p;}
static int32_t lv_obj_get_scroll_top(void *p) {(void)p;return offset;}
static int32_t lv_obj_get_scroll_bottom(void *p) {(void)p;return extent-offset;}
static int32_t lv_obj_get_scroll_y(void *p) {(void)p;return offset;}
static void lv_obj_scroll_to_y(void *p,int32_t y,int anim) {(void)p;(void)anim;offset=y;}
static void position_scroll_bar(menu_component_t *m,int32_t y,int32_t max) {
 (void)m;assert(y>=0 && y<=max);thumb=y;
}
static void lv_obj_add_flag(void *p,int f) {(void)p;(void)f;hidden=1;}
static void lv_obj_remove_flag(void *p,int f) {(void)p;(void)f;hidden=0;}
"""
tests = r"""
int main(void) {
 menu_component_t m={.items_cont=(void *)1};
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
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="menu-scroll-") as tmp:
    path=Path(tmp); c=path/"test.c"; binary=path/"test"
    c.write_text(fakes+code+tests)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined",str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("PASS: empty content, repeated scrolling, extremes and content shrink")
