"""Converts Vortex64 (Windows) key bindings saved with the old key bug into the right keys.

The old Windows build took Qt's native scan code (the PC/AT set-1 code on Windows) as an X keycode and
subtracted 8, so the key at evdev/set-1 code e was saved as SDL(linux_table[e - 8]): W -> "8", A -> "U".
Fixed: SDL(linux_table[e]). This finds e from the saved SDL code and saves the key the user really pressed.
A binding that still holds its default value (InputSettings.cpp) was never captured, so it is already right
and is left alone.
Usage: python vx_migrate_keys.py <LinuxScancodes.h> <SDL_scancode.h> <InputSettings.cpp> <in Project64.cfg> <out Project64.cfg>
"""
import re
import sys

lin_h, sdl_h, settings_cpp, src, dst = sys.argv[1:6]
sdl = {}
for m in re.finditer(r'(SDL_SCANCODE_\w+)\s*=\s*(\d+)', open(sdl_h, encoding='utf-8', errors='replace').read()):
    sdl[m.group(1)] = int(m.group(2))
text = open(lin_h, encoding='utf-8', errors='replace').read()
table_src = text[text.index('linux_scancode_table[] = {'):]
table_src = table_src[:table_src.index('};')]
table = [sdl.get(m.group(1), 0) for m in re.finditer(r'/\*\s*\d+, 0x[0-9a-f]+ \*/\s*(SDL_SCANCODE_\w+)', table_src)]
inverse = {}
for e, code in enumerate(table):
    if code and code not in inverse:
        inverse[code] = e

KEYBOARD = '{A15C6E10-0000-4000-8000-000000000001}'
cpp = open(settings_cpp, encoding='utf-8', errors='replace').read()
values = dict(re.findall(r'static const char \* ?(\w+_Default) = "(.*?)";', cpp))
defaults = {}  # (section, key) -> default text
for var_key, section, var in re.findall(r'RegisterSetting\(\w+, Data_String_General, "(\w+)", "(Controller \d)", 0, (\w+_Default)\)', cpp):
    defaults[('Input-' + section, var_key)] = values.get(var, '')
out, changed = [], []
section = ''
for line in open(src, encoding='utf-8', errors='replace').read().splitlines():
    if line.startswith('['):
        section = line.strip('[]')
    m = re.match(r'^(\w+)=(\{[0-9A-F-]+\}) ([0-9A-Fa-f]+) (\d+) 5$', line)
    if m and m.group(2) == KEYBOARD and defaults.get((section, m.group(1)), '').upper() != line.split('=', 1)[1].upper()             and int(defaults.get((section, m.group(1)), KEYBOARD + ' -1 0 5').split()[1], 16) != int(m.group(3), 16):
        old = int(m.group(3), 16)
        e = inverse.get(old)
        if e is not None and e + 8 < len(table) and table[e + 8]:
            new = table[e + 8]
            line = '%s=%s %X %s 5' % (m.group(1), KEYBOARD, new, m.group(4))
            changed.append('%s: %d -> %d' % (m.group(1), old, new))
    out.append(line)
open(dst, 'w', encoding='utf-8', newline='\r\n').write('\n'.join(out) + '\n')
print('\n'.join(changed) if changed else 'nothing to change')
