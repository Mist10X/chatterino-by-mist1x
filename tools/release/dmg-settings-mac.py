# -*- coding: utf-8 -*-
# dmgbuild settings of the macOS disk image, see build-release-mac.sh:
#   dmgbuild -s dmg-settings-mac.py -D app=<.app> -D readme=<file> <volume> <dmg>
import os.path
import plistlib

application = defines['app']
readme = defines['readme']
appname = os.path.basename(application)


def icon_from_app(app_path):
    with open(os.path.join(app_path, 'Contents', 'Info.plist'), 'rb') as f:
        name = plistlib.load(f)['CFBundleIconFile']
    if not os.path.splitext(name)[1]:
        name += '.icns'
    return os.path.join(app_path, 'Contents', 'Resources', name)


format = 'UDBZ'
files = [application, readme]
symlinks = {'Applications': '/Applications'}
badge_icon = icon_from_app(application)

icon_locations = {
    appname: (140, 120),
    'Applications': (500, 120),
    os.path.basename(readme): (320, 240),
}

background = 'builtin-arrow'
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
window_rect = ((100, 100), (640, 360))
default_view = 'icon-view'
arrange_by = None
grid_offset = (0, 0)
grid_spacing = 100
label_pos = 'bottom'
text_size = 14
icon_size = 112
