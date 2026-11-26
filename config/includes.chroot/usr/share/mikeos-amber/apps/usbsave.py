#!/usr/bin/env python3
"""Mike OS: Save Changes to USB.

When Mike OS runs straight from a USB stick, everything is forgotten when you turn it off. This sets up the
empty space on the stick so your accounts, files, apps and settings are kept from now on.
"""
import os
import subprocess
import sys

from PyQt5.QtCore import QPoint, QProcess, Qt
from PyQt5.QtGui import QColor, QIcon, QLinearGradient, QPainter, QPixmap
from PyQt5.QtWidgets import QApplication, QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget

LOGO = "/usr/share/pixmaps/mikeos-amber.svg"
HELPER = os.environ.get("AMBER_USB_SAVE", "/usr/share/mikeos-amber/bin/amber-usb-save")

STYLE = """
QWidget#root { background: #f3e8ff; }
QLabel { color: #1c0a2e; font-size: 11pt; }
QLabel#headTitle { color: #1f1a1a; font-size: 20pt; font-weight: bold; }
QLabel#headText { color: #2a1000; font-size: 11pt; }
QLabel#big { font-size: 14pt; font-weight: bold; color: #3a1f5c; }
QLabel#error { color: #b3261e; font-weight: bold; }
QLabel#hint { color: #5d4878; font-size: 9pt; }
QPushButton { background: #fbf7ff; color: #1c0a2e; border: 1px solid #a98bc0; border-radius: 5px; padding: 6px 16px; }
QPushButton:hover { background: #fff3dc; }
QPushButton#primary { background: #f08a00; color: #1a0f00; border: 1px solid #b44a00; font-weight: bold; padding: 7px 22px; }
QPushButton#primary:hover { background: #ffa21a; }
QPushButton#primary:disabled { background: #e8d9c4; color: #8a7a6a; border-color: #d8c8b0; }
"""


def saving_on():
    """Is Mike OS already keeping changes on the stick this time?"""
    try:
        return any(" /run/live/persistence/" in ln for ln in open("/proc/mounts"))
    except OSError:
        return False


def gb(n):
    return "%.1f GB" % (int(n) / 1e9)


class Header(QWidget):
    def __init__(self, title, text):
        super().__init__()
        self.setFixedHeight(110)
        self.logo = QPixmap(LOGO)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(128, 22, 20, 18)
        t = QLabel(title)
        t.setObjectName("headTitle")
        s = QLabel(text)
        s.setObjectName("headText")
        lay.addWidget(t)
        lay.addWidget(s)
        lay.addStretch(1)

    def paintEvent(self, _):
        p = QPainter(self)
        g = QLinearGradient(0, 0, self.width(), self.height())
        g.setColorAt(0, QColor("#ffc247"))
        g.setColorAt(0.55, QColor("#f08a00"))
        g.setColorAt(1, QColor("#b44a00"))
        p.fillRect(self.rect(), g)
        w, h = self.width(), self.height()
        for i, c in enumerate(("#b508de", "#1e9e3a", "#1560d8")):
            x = w - 120 + i * 20
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(c))
            p.drawPolygon(QPoint(x, h), QPoint(x + 14, h), QPoint(x + 14 + h, 0), QPoint(x + h, 0))
        p.fillRect(0, h - 4, w, 4, QColor("#7a12c4"))
        if not self.logo.isNull():
            p.drawPixmap(22, 12, self.logo.scaled(84, 84, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        p.end()


class SaveSetup(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("root")
        self.setAttribute(Qt.WA_StyledBackground)
        self.setWindowTitle("Save Changes to USB")
        self.setWindowIcon(QIcon.fromTheme("drive-removable-media", QIcon(LOGO)))
        self.setStyleSheet(STYLE)
        self.setFixedWidth(540)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        lay.addWidget(Header("Save Changes to USB", "Keep your stuff on the USB stick."))
        body = QVBoxLayout()
        body.setContentsMargins(26, 18, 26, 18)
        body.setSpacing(10)
        lay.addLayout(body)
        self.title = QLabel("")
        self.title.setObjectName("big")
        self.title.setWordWrap(True)
        body.addWidget(self.title)
        self.text = QLabel("Checking your USB stick...")
        self.text.setWordWrap(True)
        self.text.setTextFormat(Qt.RichText)
        body.addWidget(self.text)
        self.hint = QLabel("")
        self.hint.setObjectName("hint")
        self.hint.setWordWrap(True)
        body.addWidget(self.hint)
        self.error = QLabel("")
        self.error.setObjectName("error")
        self.error.setWordWrap(True)
        body.addWidget(self.error)
        body.addStretch(1)
        row = QHBoxLayout()
        self.close_btn = QPushButton("Close")
        self.close_btn.clicked.connect(self.close)
        row.addWidget(self.close_btn)
        row.addStretch(1)
        self.go = QPushButton("Set Up Saving")
        self.go.setObjectName("primary")
        self.go.hide()
        self.go.clicked.connect(self.setup)
        row.addWidget(self.go)
        body.addLayout(row)
        self.setMinimumHeight(380)
        self.info = {}
        if saving_on():
            self.title.setText("Saving is already on")
            self.text.setText("Mike OS is keeping everything you do on this USB stick: your accounts, files, apps "
                              "and settings.")
        else:
            self.run("--check")

    def run(self, mode):
        self.out = ""
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.readyRead.connect(self.read)
        self.proc.finished.connect(lambda code, _s: self.finished(mode, code))
        if os.geteuid() == 0:
            self.proc.start(HELPER, [mode])
        else:
            self.proc.start("pkexec", [HELPER, mode])

    def read(self):
        chunk = bytes(self.proc.readAll()).decode(errors="replace")
        self.out += chunk
        for ln in chunk.splitlines():
            if ln.startswith("STATUS:"):
                self.text.setText(ln[7:])
            elif ln.startswith("INFO:") and "=" in ln:
                k, v = ln[5:].split("=", 1)
                self.info[k] = v

    def finished(self, mode, code):
        lines = self.out.splitlines()
        err = next((ln[6:] for ln in lines if ln.startswith("ERROR:")), "")
        ok = next((ln[3:] for ln in lines if ln.startswith("OK:")), "")
        if err or not ok:
            self.title.setText("Saving can't be set up")
            self.text.setText("")
            self.error.setText(err or ("That was cancelled." if code in (126, 127) else "Something went wrong."))
            self.go.hide()
            self.close_btn.setEnabled(True)
            return
        if ok == "already":
            self.title.setText("This USB stick is ready to save")
            self.text.setText("Restart Mike OS and everything you do will be kept on this USB stick.")
            self.show_restart()
        elif ok == "ready":
            self.title.setText("Keep your stuff on this USB stick?")
            self.text.setText("Right now Mike OS forgets everything when you turn it off. Mike OS will use the "
                              "<b>%s</b> of empty space on <b>%s</b> (%s) to keep your accounts, files, apps and "
                              "settings." % (gb(self.info.get("free", 0)), self.info.get("name", "your USB stick"),
                                             gb(self.info.get("size", 0))))
            self.hint.setText("Mike OS itself isn't changed, and it will still start on any computer. "
                              "Don't unplug the USB stick while this runs.")
            self.go.show()
        elif ok == "done":
            self.title.setText("All set!")
            self.text.setText("Restart Mike OS from this USB stick, and from then on everything you do is kept.")
            self.hint.setText("")
            self.show_restart()

    def show_restart(self):
        self.go.setText("Restart Now")
        self.go.setEnabled(True)
        self.go.show()
        try:
            self.go.clicked.disconnect()
        except TypeError:
            pass
        self.go.clicked.connect(lambda: subprocess.Popen(["systemctl", "reboot"]))
        self.close_btn.setEnabled(True)

    def setup(self):
        self.go.setEnabled(False)
        self.close_btn.setEnabled(False)
        self.hint.setText("Don't unplug the USB stick while this runs.")
        self.text.setText("Starting...")
        self.run("--setup")

    def closeEvent(self, ev):
        if not self.close_btn.isEnabled():
            ev.ignore()          # don't close in the middle of setting up
        else:
            ev.accept()


def light_palette(app):
    from PyQt5.QtGui import QPalette
    if app.palette().color(QPalette.Window).lightness() < 128:
        pal = QPalette()
        for role, col in ((QPalette.Window, "#f3e8ff"), (QPalette.WindowText, "#1c0a2e"), (QPalette.Base, "#ffffff"),
                          (QPalette.Text, "#1c0a2e"), (QPalette.Button, "#fbf7ff"), (QPalette.ButtonText, "#1c0a2e"),
                          (QPalette.Highlight, "#f08a00"), (QPalette.HighlightedText, "#1a0f00")):
            pal.setColor(role, QColor(col))
        app.setPalette(pal)


def main():
    app = QApplication(sys.argv)
    app.setDesktopFileName("mikeos-amber-usb-save")
    light_palette(app)
    w = SaveSetup()
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
