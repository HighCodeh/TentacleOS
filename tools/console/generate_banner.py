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

"""Generate the console mascot from the flash tool's canonical artwork."""
import sys, json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'highboy'))
from flash_visual import mascot_lines
out = Path(sys.argv[1])
notice = Path(__file__).read_text().split('"""')[0].splitlines()[1:]
text = '\n'.join('//' + line[1:] for line in notice) + '\n'
for name, color in [('COLOR', True), ('PLAIN', False)]:
    text += 'static const char CONSOLE_OCTOBIT_' + name + '[] =\n'
    text += '\n'.join(json.dumps(line + '\n').replace('\\u001b', '\\033') for line in mascot_lines(color)) + ';\n'
out.parent.mkdir(parents=True, exist_ok=True)
if not out.exists() or out.read_text() != text: out.write_text(text)
