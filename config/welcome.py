#!/usr/bin/env python3
"""Welcome Center - Mike OS: Amber. Opens at login until "Show at startup" is unticked."""
import glob
import os
import shutil
import subprocess
import sys

from PyQt5.QtCore import Qt, QPoint, pyqtSignal
from PyQt5.QtGui import QIcon, QPixmap, QPainter, QLinearGradient, QColor
from PyQt5.QtWidgets import (QApplication, QCheckBox, QFrame, QGridLayout, QHBoxLayout, QLabel,
                             QMenu, QPushButton, QVBoxLayout, QWidget)

LOGO = "/usr/share/pixmaps/mikeos-amber.svg"
SOUNDS = "/usr/share/sounds/mikeos-amber"
CONF = os.path.join(os.path.expanduser("~"), ".config", "mikeos-amber-welcome")
VERSION = "Version 1 (Amber)"


def os_version():
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("VERSION="):
                    return "Version " + line.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return VERSION


def show_at_startup():
    try:
        with open(CONF) as f:
            return f.read().strip() != "show=0"
    except OSError:
        return True


def set_show_at_startup(on):
    try:
        os.makedirs(os.path.dirname(CONF), exist_ok=True)
        with open(CONF, "w") as f:
            f.write("show=1\n" if on else "show=0\n")
    except OSError:
        pass


def click_sound():
    player = shutil.which("pw-play") or shutil.which("paplay")
    path = os.path.join(SOUNDS, "click.ogg")
    if player and os.path.exists(path):
        try:
            subprocess.Popen([player, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass


def installer_desktop_file():
    """The 'Install' launcher, only while running live from the USB stick."""
    if not os.path.isdir("/run/live"):
        return None
    for path in sorted(glob.glob("/usr/share/applications/*.desktop")):
        try:
            with open(path, errors="replace") as f:
                if any(l.startswith("Exec=") and "calamares" in l or l.startswith("Exec=install-debian")
                       for l in f):
                    return path
        except OSError:
            continue
    return None


def launch(cmd):
    """Start a program without waiting for it. cmd is a list; returns True if it started."""
    if not cmd or not shutil.which(cmd[0]):
        return False
    try:
        subprocess.Popen(cmd, start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except OSError:
        return False


def first(*cmds):
    """The first command whose program is installed, or None."""
    for c in cmds:
        if c and shutil.which(c[0]):
            return c
    return None


GAMES = [
    ("Snake", "amber-snake", ["python3", "/usr/share/mikeos-amber/apps/snake.py"]),
    ("Space Cadet Pinball", "SpaceCadetPinball", ["/opt/SpaceCadetPinball/SpaceCadetPinball"]),
    ("Solitaire", "kpat", ["kpat"]),
    ("Minesweeper", "kmines", ["kmines"]),
]


class Tile(QFrame):
    clicked = pyqtSignal()

    def __init__(self, icon_names, title, text):
        super().__init__()
        self.setObjectName("tile")
        self.setCursor(Qt.PointingHandCursor)
        self.setFocusPolicy(Qt.StrongFocus)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 10, 12, 10)
        lay.setSpacing(12)
        icon = QIcon()
        for n in icon_names:
            icon = QIcon.fromTheme(n)
            if not icon.isNull():
                break
        pic = QLabel()
        pic.setFixedSize(48, 48)
        if not icon.isNull():
            pic.setPixmap(icon.pixmap(48, 48))
        lay.addWidget(pic, 0, Qt.AlignTop)
        col = QVBoxLayout()
        col.setSpacing(2)
        t = QLabel(title)
        t.setObjectName("tileTitle")
        d = QLabel(text)
        d.setObjectName("tileText")
        d.setWordWrap(True)
        col.addWidget(t)
        col.addWidget(d)
        col.addStretch(1)
        lay.addLayout(col, 1)

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton and self.rect().contains(e.pos()):
            click_sound()
            self.clicked.emit()

    def keyPressEvent(self, e):
        if e.key() in (Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space):
            click_sound()
            self.clicked.emit()
        else:
            super().keyPressEvent(e)


class Header(QWidget):
    def __init__(self):
        super().__init__()
        self.setFixedHeight(150)
        self.logo = QPixmap(LOGO)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(175, 30, 20, 26)
        title = QLabel("Welcome to Mike OS: Amber")
        title.setObjectName("headTitle")
        sub = QLabel("Your computer is ready. Here are a few places to start.")
        sub.setObjectName("headText")
        lay.addWidget(title)
        lay.addWidget(sub)
        lay.addStretch(1)

    def paintEvent(self, _):
        p = QPainter(self)
        g = QLinearGradient(0, 0, self.width(), self.height())
        g.setColorAt(0, QColor("#ffc247"))
        g.setColorAt(0.55, QColor("#f08a00"))
        g.setColorAt(1, QColor("#b44a00"))
        p.fillRect(self.rect(), g)
        # purple / green / blue stripes, like the wallpaper
        w, h = self.width(), self.height()
        for i, c in enumerate(("#b508de", "#1e9e3a", "#1560d8")):
            x = w - 150 + i * 22
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(c))
            p.drawPolygon(QPoint(x, h), QPoint(x + 16, h), QPoint(x + 16 + h, 0), QPoint(x + h, 0))
        p.fillRect(0, h - 4, w, 4, QColor("#7a12c4"))
        if not self.logo.isNull():
            p.drawPixmap(28, 17, self.logo.scaled(116, 116, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        p.end()


STYLE = """
QWidget#root { background: #f3e8ff; }
QLabel#headTitle { color: #1f1a1a; font-size: 24pt; font-weight: bold; }
QLabel#headText { color: #2a1000; font-size: 11pt; }
QFrame#tile {
    background: #fbf7ff; border: 2px solid; border-color: #ffffff #883cb5 #883cb5 #ffffff;
}
QFrame#tile:hover { background: #fff3dc; border-color: #ffb629 #b44a00 #b44a00 #ffb629; }
QFrame#tile:focus { border-color: #f08a00; }
QLabel#tileTitle { color: #3a1f5c; font-size: 12pt; font-weight: bold; }
QLabel#tileText { color: #3d2a52; font-size: 9.5pt; }
QLabel#version { color: #5d4878; }
QCheckBox { color: #1c0a2e; }
"""


class Welcome(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("root")
        self.setWindowTitle("Welcome to Mike OS: Amber")
        if os.path.exists(LOGO):
            self.setWindowIcon(QIcon(LOGO))
        self.setFixedSize(780, 540)
        self.setStyleSheet(STYLE)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        outer.addWidget(Header())

        body = QWidget()
        grid = QGridLayout(body)
        grid.setContentsMargins(22, 20, 22, 10)
        grid.setSpacing(14)

        tiles = []
        installer = installer_desktop_file()
        if installer:
            tiles.append((["system-software-install", "drive-harddisk"], "Install Mike OS",
                          "Put Mike OS: Amber on this computer's hard drive.",
                          lambda: launch(first(["kioclient5", "exec", installer], ["gtk-launch", os.path.basename(installer)]))))
        tiles += [
            (["internet-web-browser", "firefox-esr"], "Browse the Web",
             "Search Google and visit your favorite sites.",
             lambda: launch(first(["firefox-esr"], ["xdg-open", "https://www.google.com"]))),
            (["system-file-manager", "folder"], "My Files",
             "Documents, pictures, music and USB drives.",
             lambda: launch(first(["dolphin"], ["xdg-open", os.path.expanduser("~")]))),
            (["applications-games", "input-gaming"], "Play Games",
             "Snake, Space Cadet Pinball, Solitaire and more.", self.games_menu),
            (["preferences-desktop", "systemsettings"], "Personalize",
             "Change the wallpaper, sounds, colors and more.",
             lambda: launch(first(["systemsettings5"], ["systemsettings"]))),
            (["help-about", "hwinfo"], "About This PC",
             "See your computer's specs and system details.",
             lambda: launch(first(["kinfocenter"]))),
        ]
        self.tile_widgets = []
        for i, (icons, title, text, action) in enumerate(tiles):
            t = Tile(icons, title, text)
            t.clicked.connect(action)
            grid.addWidget(t, i // 2, i % 2)
            self.tile_widgets.append(t)
        outer.addWidget(body, 1)

        foot = QHBoxLayout()
        foot.setContentsMargins(22, 6, 22, 16)
        self.startup = QCheckBox("Show this window at startup")
        self.startup.setChecked(show_at_startup())
        self.startup.toggled.connect(set_show_at_startup)
        ver = QLabel(os_version())
        ver.setObjectName("version")
        close = QPushButton("Close")
        close.setMinimumWidth(90)
        close.clicked.connect(self.close)
        foot.addWidget(self.startup)
        foot.addStretch(1)
        foot.addWidget(ver)
        foot.addSpacing(14)
        foot.addWidget(close)
        outer.addLayout(foot)

    def games_menu(self):
        menu = QMenu(self)
        for name, icon, cmd in GAMES:
            if shutil.which(cmd[0]) and (cmd[0] != "python3" or os.path.exists(cmd[1])):
                act = menu.addAction(QIcon.fromTheme(icon), name)
                act.triggered.connect(lambda _=False, c=cmd: launch(c))
        if menu.isEmpty():
            menu.addAction("No games installed").setEnabled(False)
        tile = self.sender()
        pos = tile.mapToGlobal(tile.rect().bottomLeft()) if tile else self.cursor().pos()
        menu.exec_(pos)


def main():
    if "--autostart" in sys.argv and not show_at_startup():
        return
    app = QApplication(sys.argv)
    app.setApplicationName("Welcome")
    app.setDesktopFileName("mikeos-amber-welcome")
    w = Welcome()
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
