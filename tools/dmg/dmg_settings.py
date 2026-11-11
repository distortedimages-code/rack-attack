# dmgbuild settings for RackAttack-Editor-mac.dmg (see make_dmg.sh).
# Three rows: each bundle on the left, the folder it goes in on the right. Drag one onto the other.
import os

src = defines["src"]              # folder holding the three bundles

format = "UDZO"
filesystem = "HFS+"
files = [
    (os.path.join(src, "RackAttack Editor.component"), "RackAttack Editor.component"),
    (os.path.join(src, "RackAttack Editor.vst3"), "RackAttack Editor.vst3"),
    (os.path.join(src, "RackAttack Editor.app"), "RackAttack Editor.app"),
]
symlinks = {
    "Components (AU)": "/Library/Audio/Plug-Ins/Components",
    "VST3": "/Library/Audio/Plug-Ins/VST3",
    "Applications": "/Applications",
}
icon_locations = {
    "RackAttack Editor.component": (150, 110),
    "Components (AU)": (450, 110),
    "RackAttack Editor.vst3": (150, 230),
    "VST3": (450, 230),
    "RackAttack Editor.app": (150, 350),
    "Applications": (450, 350),
}
window_rect = ((200, 140), (600, 460))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 64
text_size = 12
arrange_by = None
