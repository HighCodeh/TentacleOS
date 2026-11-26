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

"""Exercise actual command/hint/path callbacks with a temporary directory."""
import subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
fixture=r"""
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "console_editor.c"
static const char *cwd;
const char *console_editor_cwd(void) { return cwd; }
void linenoiseAddCompletion(linenoiseCompletions *out, const char *value) {
 out->cvec=realloc(out->cvec,(out->len+1)*sizeof(char*));assert(out->cvec);
 out->cvec[out->len++]=strdup(value);
}
void esp_console_get_completion(const char *line, linenoiseCompletions *out) {
 const char *names[]={"battery","wifi","cat","cd"};
 for(unsigned i=0;i<4;i++)if(!strncmp(names[i],line,strlen(line)))linenoiseAddCompletion(out,names[i]);
}
const char *esp_console_get_hint(const char *line,int *color,int *bold) {
 (void)color;(void)bold;return !strcmp(line,"battery")?" [regs|profiles]":NULL;
}
void linenoiseSetCompletionCallback(void (*fn)(const char *,linenoiseCompletions *)){assert(fn==complete);}
void linenoiseSetHintsCallback(char *(*fn)(const char *,int *,int *)){assert(fn==hint);}
static void check(const char *line,const char *expected) {
 linenoiseCompletions out={0};complete(line,&out);bool found=false;
 for(size_t i=0;i<out.len;i++){if(!strcmp(out.cvec[i],expected))found=true;free(out.cvec[i]);}
 free(out.cvec);assert(found);
}
int main(int argc,char **argv) {
 assert(argc==2);cwd=argv[1];console_editor_init();
 check("bat","battery");check("c5 spi","c5 spiclock");check("battery charge o","battery charge on");
 int color=0,bold=1;assert(!strcmp(hint("bat",&color,&bold),"tery")&&color==90&&bold==0);
 assert(!strcmp(hint("battery",&color,&bold)," [regs|profiles]"));
 char filename[512];snprintf(filename,sizeof(filename),"%s/test.txt",cwd);
 FILE *f=fopen(filename,"w");assert(f);fclose(f);
 check("cat te","cat test.txt");check("ls te","ls test.txt");
 linenoiseCompletions out={0};complete("cd te",&out);assert(out.len==0);
 complete("cat \"te",&out);assert(out.len==0);
 char longline[600];memset(longline,'a',sizeof(longline));memcpy(longline,"cat /",5);longline[599]=0;
 complete(longline,&out);assert(out.len==0);
 unlink(filename);return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='console-editor-') as tmp:
 p=Path(tmp);(p/'linenoise').mkdir()
 (p/'linenoise/linenoise.h').write_text('#pragma once\n#include <stddef.h>\n#include <stdbool.h>\ntypedef struct {size_t len;char **cvec;} linenoiseCompletions;\nvoid linenoiseAddCompletion(linenoiseCompletions*,const char*);\nvoid linenoiseSetCompletionCallback(void(*)(const char*,linenoiseCompletions*));\nvoid linenoiseSetHintsCallback(char*(*)(const char*,int*,int*));\n')
 (p/'esp_console.h').write_text('#include "linenoise/linenoise.h"\nvoid esp_console_get_completion(const char*,linenoiseCompletions*);\nconst char *esp_console_get_hint(const char*,int*,int*);\n')
 (p/'esp_log.h').write_text('#define ESP_LOGD(tag,...) ((void)(tag))\n')
 (p/'test.c').write_text(fixture)
 source=ROOT/'firmware_p4/components/Service/console'
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-sign-compare','-Werror','-fsanitize=address,undefined','-I',str(p),'-I',str(source),'-I',str(source/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),tmp],check=True)
print('PASS: command/subcommand hints, bounded path completion, directory filtering and unsupported quoting')
