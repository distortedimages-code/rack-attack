# dmgbuild settings for RackAttack-Editor-mac.dmg (see make_dmg.sh).
# The window: the installer on the left; on the right the three bundles, each beside a shortcut to its
# folder (for those who drag by hand); the read-me under the installer.
# The icon places match draw_background.py (the background is drawn around them).
import os

stage = defines["stage"]          # Installer RackAttack Editor.pkg, the three bundles, LISEZ-MOI.txt
background = defines["bg"]        # background.tiff (1x + 2x)

format = "UDZO"
filesystem = "HFS+"
files = [os.path.join(stage, n) for n in ("Installer RackAttack Editor.pkg", "RackAttack Editor.component",
                                           "RackAttack Editor.vst3", "RackAttack Editor.app", "LISEZ-MOI.txt")]
symlinks = {
    "Components": "/Library/Audio/Plug-Ins/Components",
    "VST3": "/Library/Audio/Plug-Ins/VST3",
    "Applications": "/Applications",
}
icon_locations = {
    "Installer RackAttack Editor.pkg": (150, 235),
    "LISEZ-MOI.txt": (150, 420),
    "RackAttack Editor.component": (420, 180), "Components": (610, 180),
    "RackAttack Editor.vst3": (420, 290), "VST3": (610, 290),
    "RackAttack Editor.app": (420, 400), "Applications": (610, 400),
}
window_rect = ((200, 120), (720, 520))
default_view = "icon-view"
show_icon_preview = False
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 64
text_size = 12
arrange_by = None
label_pos = "bottom"
