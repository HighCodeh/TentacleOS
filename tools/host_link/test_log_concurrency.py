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

"""Check production log hooks while another task is writing a Companion frame."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service/host_link"

def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]

core = (SERVICE / "host_link.c").read_text()
log = (SERVICE / "host_link_log.c").read_text()
state = re.search(r"^static .*s_is_emitting = false;.*$", core, re.M).group()
code = r"""
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
static int log_vprintf(const char *, va_list);
static int emit(const char *fmt, ...) {
 va_list ap; va_start(ap, fmt); int n=log_vprintf(fmt,ap); va_end(ap); return n;
}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static bool ready, finished;
static _Thread_local unsigned local_lines, relay_lines;
static void *s_log_queue=(void*)1;
static int previous(const char *fmt, va_list ap) {
 (void)fmt;(void)ap;local_lines++;
 assert(emit("recursive UART driver error")==0);
 return 7;
}
static int (*s_prev_vprintf)(const char*,va_list)=previous;
static unsigned host_link_output_generation(void){return 1;}
static bool busb_msc_is_exposed(void){return false;}
static void queue_relay_line(const char *fmt,va_list ap){(void)fmt;(void)ap;relay_lines++;}
""" + state + "\n" + function(core, "bool host_link_is_emitting(void)") + "\n" + function(log, "static int log_vprintf(") + r"""
static void *writer(void *unused) {
 (void)unused;s_is_emitting=true;
 pthread_mutex_lock(&lock);ready=true;pthread_cond_signal(&cond);
 while(!finished)pthread_cond_wait(&cond,&lock);
 pthread_mutex_unlock(&lock);
 assert(host_link_is_emitting());
 assert(emit("writer transport error")==7);
 assert(local_lines==1 && relay_lines==0);
 s_is_emitting=false;
 assert(emit("writer completed")==7);
 assert(local_lines==2 && relay_lines==1);
 return NULL;
}
int main(void) {
 pthread_t thread;assert(pthread_create(&thread,NULL,writer,NULL)==0);
 pthread_mutex_lock(&lock);
 while(!ready)pthread_cond_wait(&cond,&lock);
 pthread_mutex_unlock(&lock);
 assert(!host_link_is_emitting());
 assert(emit("Ethernet timeout from another task")==7);
 assert(local_lines==1 && relay_lines==1);
 pthread_mutex_lock(&lock);finished=true;pthread_cond_signal(&cond);pthread_mutex_unlock(&lock);
 assert(pthread_join(thread,NULL)==0);
 puts("Companion concurrent logging and recursion checks passed");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-log-test-") as tmp:
    source = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    source.write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
