#!/usr/bin/env python3
"""Mike OS Themes - switch the look of Mike OS: taskbar, start button and menu, window title bars,
window colours and wallpaper. Themes come from the Amber Store; Amber is always there.

  themes.py                 open the Themes window
  themes.py --apply ID      switch to a theme (amber, xp-blue, ...)
  themes.py --list          show the installed themes
  themes.py --current       show the theme in use

Works on the full desktop (KDE Plasma) and on Amber Lite (LXQt + Openbox). Everything is changed
for the current user only - no password needed."""
import glob
import json
import os
import re
import shutil
import subprocess
import sys

THEMES_DIR = os.environ.get("AMBER_THEMES_DIR", "/usr/share/mikeos-amber/themes")
HOME = os.path.expanduser("~")
CONFIG = os.environ.get("XDG_CONFIG_HOME", os.path.join(HOME, ".config"))
CURRENT_FILE = os.path.join(CONFIG, "mikeos-amber", "theme")
SAVED_PANEL = os.path.join(CONFIG, "mikeos-amber", "panel.conf.saved")       # your own taskbar layout, kept
KDE_DOCK_FILE = os.path.join(CONFIG, "mikeos-amber", "kde-dock")              # while a theme moves things around
DEFAULT_TITLE_LAYOUT = "NLIMC"


# ------------------------------------------------------------------ themes on disk
def load_themes():
    themes = []
    for f in sorted(glob.glob(os.path.join(THEMES_DIR, "*", "theme.json"))):
        try:
            with open(f) as fh:
                t = json.load(fh)
            if t.get("id") and t.get("name"):
                themes.append(t)
        except (OSError, ValueError):
            pass
    themes.sort(key=lambda t: (t["id"] != "amber", t["name"].lower()))
    return themes


def find_theme(tid):
    for t in load_themes():
        if t["id"] == tid:
            return t
    return None


def current_theme():
    try:
        with open(CURRENT_FILE) as f:
            tid = f.read().strip()
        if find_theme(tid):
            return tid
    except OSError:
        pass
    return "amber"


def desktop():
    d = (os.environ.get("XDG_CURRENT_DESKTOP", "") + ":" + os.environ.get("DESKTOP_SESSION", "")).lower()
    if "kde" in d or "plasma" in d:
        return "kde"
    if "lxqt" in d:
        return "lxqt"
    if run_ok(["pgrep", "-u", str(os.getuid()), "-x", "plasmashell"]):
        return "kde"
    return "lxqt"


def run_ok(cmd, **kw):
    try:
        return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30, **kw).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def first_tool(*names):
    for n in names:
        if shutil.which(n):
            return n
    return None


# ------------------------------------------------------------------ small ini editor (keeps the file's own formatting)
def ini_set(path, section, values):
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        lines = []
    start = None
    for i, l in enumerate(lines):
        if l.strip() == "[%s]" % section:
            start = i
            break
    if start is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines.append("[%s]" % section)
        start = len(lines) - 1
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("["):
            end = j
            break
    todo = dict(values)
    for j in range(start + 1, end):
        k = lines[j].split("=", 1)[0].strip()
        if k in todo:
            lines[j] = "%s=%s" % (k, todo.pop(k))
    insert = ["%s=%s" % kv for kv in todo.items()]
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    lines[end:end] = insert
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


# ------------------------------------------------------------------ applying
def apply_lxqt(t):
    lx = t.get("lxqt", {})
    conf = os.path.join(CONFIG, "lxqt", "lxqt.conf")
    if lx.get("theme"):
        ini_set(conf, "General", {"theme": lx["theme"]})
    if lx.get("palette"):
        ini_set(conf, "Palette", lx["palette"])
    # window borders (Openbox)
    if lx.get("openbox"):
        ob = os.path.join(CONFIG, "openbox")
        os.makedirs(ob, exist_ok=True)
        for name in ("lxqt-rc.xml", "rc.xml"):
            path = os.path.join(ob, name)
            if not os.path.exists(path):
                for src in ("/etc/xdg/openbox/lxqt-rc.xml", "/etc/xdg/openbox/rc.xml"):
                    if os.path.exists(src):
                        shutil.copyfile(src, path)
                        break
            try:
                with open(path) as f:
                    xml = f.read()
            except OSError:
                continue
            new = re.sub(r"(<theme>.*?<name>)[^<]*(</name>)", lambda m: m.group(1) + lx["openbox"] + m.group(2),
                         xml, count=1, flags=re.S)
            layout = lx.get("title_layout", DEFAULT_TITLE_LAYOUT)       # where the window buttons go
            new = re.sub(r"(<theme>.*?<titleLayout>)[^<]*(</titleLayout>)", lambda m: m.group(1) + layout + m.group(2),
                         new, count=1, flags=re.S)
            if new != xml:
                with open(path, "w") as f:
                    f.write(new)
        if shutil.which("openbox"):
            run_ok(["openbox", "--reconfigure"])
    # taskbar layout (some themes move the taskbar or add a dock)
    set_panel_layout(lx.get("panel"))
    # wallpaper
    wp = t.get("wallpaper")
    if wp and os.path.exists(wp):
        pconf = os.path.join(CONFIG, "pcmanfm-qt", "lxqt", "settings.conf")
        ini_set(pconf, "Desktop", {"Wallpaper": wp})
        if shutil.which("pcmanfm-qt"):
            subprocess.Popen(["pcmanfm-qt", "--set-wallpaper=" + wp, "--wallpaper-mode=zoom"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def restart_panel():
    """The taskbar only reads its layout when it starts."""
    if not run_ok(["pgrep", "-u", str(os.getuid()), "-x", "lxqt-panel"]):
        return
    run_ok(["pkill", "-u", str(os.getuid()), "-x", "lxqt-panel"])
    import time
    for _ in range(30):                       # the session usually starts it again by itself...
        time.sleep(0.2)
        if not run_ok(["pgrep", "-u", str(os.getuid()), "-x", "lxqt-panel"]):
            break
    time.sleep(1.5)
    if not run_ok(["pgrep", "-u", str(os.getuid()), "-x", "lxqt-panel"]):   # ...if not, we do
        subprocess.Popen(["setsid", "lxqt-panel"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         stdin=subprocess.DEVNULL)


def set_panel_layout(layout):
    """layout = a panel.conf from the theme, or None for your own layout."""
    conf = os.path.join(CONFIG, "lxqt", "panel.conf")
    if layout and os.path.exists(layout):
        if not os.path.exists(SAVED_PANEL):            # keep your layout to put back later
            os.makedirs(os.path.dirname(SAVED_PANEL), exist_ok=True)
            if os.path.exists(conf):
                shutil.copyfile(conf, SAVED_PANEL)
            elif os.path.exists("/usr/share/mikeos-amber/lite/panel.conf"):
                shutil.copyfile("/usr/share/mikeos-amber/lite/panel.conf", SAVED_PANEL)
        with open(layout) as f, open(conf, "w") as g:
            g.write(f.read())
        restart_panel()
    elif os.path.exists(SAVED_PANEL):                  # back to your own layout
        shutil.copyfile(SAVED_PANEL, conf)
        os.remove(SAVED_PANEL)
        restart_panel()


KDE_DOCK_ADD = """
var main = panels().filter(function (p) { return p.location == "bottom"; })[0] || panels()[0];
if (main) { main.location = "top"; main.height = Math.round(gridUnit * 1.6); }
var dock = new Panel;
dock.location = "bottom";
dock.alignment = "center";
dock.height = Math.round(gridUnit * 3.2);
dock.minimumLength = Math.round(gridUnit * 24);
dock.maximumLength = Math.round(gridUnit * 40);
var tasks = dock.addWidget("org.kde.plasma.icontasks");
tasks.currentConfigGroup = ["General"];
tasks.writeConfig("launchers", %s);
print(dock.id);
"""

KDE_DOCK_REMOVE = """
var id = %s;
panels().forEach(function (p) { if (p.id == id) p.remove(); });
var main = panels().filter(function (p) { return p.location == "top"; })[0];
if (main) { main.location = "bottom"; main.height = Math.round(gridUnit * 2.2); }
"""


def kde_script(qd, code):
    try:
        return subprocess.run([qd, "org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript", code],
                              capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def kde_layout(qd, dock_apps):
    """Mac-style layout on the full desktop: taskbar to the top, a dock at the bottom - and back."""
    old = None
    try:
        with open(KDE_DOCK_FILE) as f:
            old = f.read().strip()
    except OSError:
        pass
    if dock_apps and not old:
        launchers = ["file://" + a if a.startswith("/") else "applications:" + a for a in dock_apps]
        out = kde_script(qd, KDE_DOCK_ADD % json.dumps(launchers))
        if out.strip().isdigit():
            os.makedirs(os.path.dirname(KDE_DOCK_FILE), exist_ok=True)
            with open(KDE_DOCK_FILE, "w") as f:
                f.write(out.strip())
    elif not dock_apps and old:
        kde_script(qd, KDE_DOCK_REMOVE % json.dumps(int(old) if old.isdigit() else -1))
        os.remove(KDE_DOCK_FILE)


KDE_MENU_SCRIPT = """
var icon = %s;
panels().forEach(function (p) {
  p.widgets().forEach(function (w) {
    if (/kickoff|kicker|kickerdash|menu/i.test(w.type)) {
      w.currentConfigGroup = ["General"];
      w.writeConfig("icon", icon);
      w.writeConfig("useCustomButtonImage", true);
      w.writeConfig("customButtonImage", "file://" + icon);
    }
  });
});
"""


def apply_kde(t):
    k = t.get("kde", {})
    kw = first_tool("kwriteconfig5", "kwriteconfig6")
    qd = first_tool("qdbus", "qdbus-qt5", "qdbus6")
    if k.get("colorscheme") and shutil.which("plasma-apply-colorscheme"):
        run_ok(["plasma-apply-colorscheme", k["colorscheme"]])
    if k.get("desktoptheme") and shutil.which("plasma-apply-desktoptheme"):
        run_ok(["plasma-apply-desktoptheme", k["desktoptheme"]])
    deco = k.get("decoration")
    if deco and kw:
        run_ok([kw, "--file", "kwinrc", "--group", "org.kde.kdecoration2", "--key", "library", deco["library"]])
        run_ok([kw, "--file", "kwinrc", "--group", "org.kde.kdecoration2", "--key", "theme", deco["theme"]])
        if qd:
            run_ok([qd, "org.kde.KWin", "/KWin", "reconfigure"])
    wp = t.get("wallpaper")
    if wp and os.path.exists(wp) and shutil.which("plasma-apply-wallpaperimage"):
        run_ok(["plasma-apply-wallpaperimage", wp])
    if qd:
        kde_layout(qd, k.get("dock"))
    if k.get("start_icon") and qd:
        run_ok([qd, "org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript",
                KDE_MENU_SCRIPT % json.dumps(k["start_icon"])])


def apply_theme(tid):
    t = find_theme(tid)
    if not t:
        raise SystemExit("No theme called %r is installed." % tid)
    if desktop() == "kde":
        apply_kde(t)
    else:
        apply_lxqt(t)
    os.makedirs(os.path.dirname(CURRENT_FILE), exist_ok=True)
    with open(CURRENT_FILE, "w") as f:
        f.write(t["id"] + "\n")
    return t


# ------------------------------------------------------------------ the window
def gui():
    from PyQt5.QtCore import Qt, QSize, QTimer
    from PyQt5.QtGui import QIcon, QPixmap
    from PyQt5.QtWidgets import (QApplication, QHBoxLayout, QLabel, QListWidget, QListWidgetItem,
                                 QPushButton, QVBoxLayout, QWidget)

    app = QApplication(sys.argv)
    # Dark themes: this window has its own light Amber design, so keep the light Amber colours for it
    # (otherwise the dark theme's white text lands on its light backgrounds).
    from PyQt5.QtGui import QColor as _AmberC, QPalette as _AmberP
    if app.palette().color(_AmberP.Window).lightness() < 128:
        _pal = _AmberP()
        for _role, _col in ((_AmberP.Window, "#cfa5e8"), (_AmberP.WindowText, "#1c0a2e"), (_AmberP.Base, "#fbf7ff"),
                            (_AmberP.AlternateBase, "#f3e8ff"), (_AmberP.Text, "#1c0a2e"), (_AmberP.Button, "#e6d2f4"),
                            (_AmberP.ButtonText, "#1c0a2e"), (_AmberP.Highlight, "#f08a00"),
                            (_AmberP.HighlightedText, "#1a0f00"), (_AmberP.Link, "#1560d8"),
                            (_AmberP.LinkVisited, "#7a12c4"), (_AmberP.ToolTipBase, "#fbf7ff"),
                            (_AmberP.ToolTipText, "#1c0a2e"), (_AmberP.PlaceholderText, "#8a7a9a"),
                            (_AmberP.Light, "#ffffff"), (_AmberP.Midlight, "#e6d2f4"), (_AmberP.Mid, "#a98bc0"),
                            (_AmberP.Dark, "#7d5c98"), (_AmberP.Shadow, "#3d2a52")):
            _pal.setColor(_role, _AmberC(_col))
        for _role in (_AmberP.WindowText, _AmberP.Text, _AmberP.ButtonText):
            _pal.setColor(_AmberP.Disabled, _role, _AmberC("#8a7a9a"))
        app.setPalette(_pal)
    app.setApplicationName("Themes")
    app.setDesktopFileName("mikeos-amber-themes")
    w = QWidget()
    w.setWindowTitle("Themes")
    w.setWindowIcon(QIcon.fromTheme("preferences-desktop-theme", QIcon("/usr/share/pixmaps/mikeos-amber.svg")))
    w.resize(720, 460)
    w.setStyleSheet("""
        QWidget#root { background: #f7f3fc; color: #1c0a2e; }
        QLabel { color: #1c0a2e; }
        QPushButton { color: #1c0a2e; }
        QLabel#title { font-size: 16pt; font-weight: bold; color: #4a1880; }
        QLabel#name { font-size: 13pt; font-weight: bold; color: #1c0a2e; }
        QLabel#desc { color: #444; }
        QLabel#status { color: #4a1880; }
        QListWidget { background: white; color: #1c0a2e; border: 1px solid #cbb8e6; font-size: 11pt; }
        QListWidget::item { padding: 6px; }
        QListWidget::item:selected { background: #f08a00; color: #1a0f00; }
        QPushButton#apply { background: #f08a00; color: #1a0f00; font-weight: bold; padding: 8px 22px; }
        QPushButton#apply:disabled { background: #e6dcef; color: #8a7a9e; }
    """)
    w.setObjectName("root")
    lay = QVBoxLayout(w)
    title = QLabel("Themes")
    title.setObjectName("title")
    lay.addWidget(title)
    body = QHBoxLayout()
    lay.addLayout(body, 1)
    lst = QListWidget()
    lst.setFixedWidth(220)
    lst.setIconSize(QSize(64, 40))
    body.addWidget(lst)
    right = QVBoxLayout()
    body.addLayout(right, 1)
    preview = QLabel()
    preview.setAlignment(Qt.AlignCenter)
    preview.setMinimumSize(320, 200)
    name = QLabel()
    name.setObjectName("name")
    desc = QLabel()
    desc.setObjectName("desc")
    desc.setWordWrap(True)
    right.addWidget(preview, 1)
    right.addWidget(name)
    right.addWidget(desc)
    row = QHBoxLayout()
    status = QLabel()
    status.setObjectName("status")
    status.setWordWrap(True)
    apply_btn = QPushButton("Use this theme")
    apply_btn.setObjectName("apply")
    row.addWidget(status, 1)
    row.addWidget(apply_btn)
    right.addLayout(row)
    more = QLabel("Get more themes in the Amber Store (Themes).")
    more.setObjectName("desc")
    lay.addWidget(more)

    def fill():
        cur = current_theme()
        sel = lst.currentItem().data(Qt.UserRole) if lst.currentItem() else cur
        lst.clear()
        for t in load_themes():
            it = QListWidgetItem(t["name"] + ("   ✓" if t["id"] == cur else ""))
            it.setData(Qt.UserRole, t["id"])
            if t.get("preview") and os.path.exists(t["preview"]):
                it.setIcon(QIcon(t["preview"]))
            lst.addItem(it)
            if t["id"] == sel:
                lst.setCurrentItem(it)
        if not lst.currentItem() and lst.count():
            lst.setCurrentRow(0)

    def show():
        it = lst.currentItem()
        if not it:
            return
        t = find_theme(it.data(Qt.UserRole))
        if not t:
            return
        pm = QPixmap(t.get("preview", ""))
        preview.setPixmap(pm.scaled(preview.width(), preview.height(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
                          if not pm.isNull() else QPixmap())
        name.setText(t["name"])
        desc.setText(t.get("description", ""))
        on = t["id"] == current_theme()
        apply_btn.setEnabled(not on)
        apply_btn.setText("In use" if on else "Use this theme")
        status.setText("")

    def do_apply():
        it = lst.currentItem()
        if not it:
            return
        apply_btn.setEnabled(False)
        status.setText("Changing the theme...")
        QApplication.processEvents()
        try:
            t = apply_theme(it.data(Qt.UserRole))
            msg = "%s is on. Windows that were already open fully update when they're opened again." % t["name"]
        except SystemExit as ex:
            msg = str(ex)
        fill()
        show()
        status.setText(msg)

    lst.currentItemChanged.connect(lambda *_: show())
    apply_btn.clicked.connect(do_apply)
    fill()
    show()
    w.show()
    QTimer.singleShot(50, show)
    return app.exec_()


def main():
    a = sys.argv[1:]
    if a[:1] == ["--apply"] and len(a) > 1:
        t = apply_theme(a[1])
        print("Theme: " + t["name"])
        return 0
    if a[:1] == ["--list"]:
        cur = current_theme()
        for t in load_themes():
            print(("* " if t["id"] == cur else "  ") + t["id"] + "  " + t["name"])
        return 0
    if a[:1] == ["--current"]:
        print(current_theme())
        return 0
    return gui()


if __name__ == "__main__":
    sys.exit(main())
