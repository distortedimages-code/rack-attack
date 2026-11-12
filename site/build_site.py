#!/usr/bin/env python3
"""RackAttack Editor's site (GitHub Pages): site/ as it is, with the version (CMakeLists.txt) and the two Terminal commands
of the disk image (tools/dmg) written in, so that the page and the DMG always say the same thing.

  python3 site/build_site.py [out]        (default site/out)"""
import html, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, 'site')
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(SITE, 'out')


def read(*path):
    with open(os.path.join(ROOT, *path), encoding='utf-8') as f:
        return f.read()


version = re.search(r'project\(RackAttack VERSION ([0-9.]+)', read('CMakeLists.txt')).group(1)
values = {
    '@VERSION@': version,
    '@INSTALLER_CMD@': html.escape(read('tools', 'dmg', 'installer-command.txt').strip(), quote=False),
    '@TERMINAL_CMD@': html.escape(read('tools', 'dmg', 'terminal-command.txt').strip(), quote=False),
}

shutil.rmtree(OUT, ignore_errors=True)
shutil.copytree(SITE, OUT, ignore=shutil.ignore_patterns('out', 'build_site.py', '__pycache__'))
if not os.path.exists(os.path.join(OUT, 'app', 'ra.wasm')):
    print('note: site/app/ra.wasm missing (tools/web/build_wasm.sh makes it): the web version will not start')
page = os.path.join(OUT, 'index.html')
text = read('site', 'index.html')
for key, value in values.items():
    text = text.replace(key, value)
left = re.findall(r'@[A-Z_]+@', text)
if left:
    sys.exit('not replaced: ' + ', '.join(sorted(set(left))))
with open(page, 'w', encoding='utf-8') as f:
    f.write(text)
print('site', version, '->', OUT)
