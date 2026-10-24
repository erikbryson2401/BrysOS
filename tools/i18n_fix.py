import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "kernel", "src"))

s = open('apps/system_apps.cpp', encoding='utf-8').read()
s = s.replace('String t = String::format("%s\\n\\n%s", topics[i].title, topics[i].text);',
              'String t = String::format("%s\\n\\n%s", T(topics[i].title), T(topics[i].text));')
open('apps/system_apps.cpp', 'w', encoding='utf-8').write(s)

for p in ['fs/vfs.cpp']:
    s = open(p, encoding='utf-8').read()
    s = re.sub(r'set_err\(("(?:[^"\\]|\\.)*")\)', r'set_err(T(\1))', s)
    open(p, 'w', encoding='utf-8').write(s)

s = open('fs/partition.cpp', encoding='utf-8').read()
s = re.sub(r'err = ("(?:[^"\\]|\\.)*");', r'err = T(\1);', s)
if '#include "lib/i18n.h"' not in s:
    s = s.replace('#include "lib/string.h"', '#include "lib/string.h"\n#include "lib/i18n.h"', 1)
open('fs/partition.cpp', 'w', encoding='utf-8').write(s)
print("ok")
