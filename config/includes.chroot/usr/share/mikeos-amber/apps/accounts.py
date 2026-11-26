#!/usr/bin/env python3
"""Mike OS: Create Your Account.

Shows up the first time the computer starts on the shared "amber" account. You make your own account
(with your own password), and Amber turns into a Guest account anyone can use without a password.
"""
import os
import pwd
import re
import shutil
import subprocess
import sys

from PyQt5.QtCore import QPoint, QProcess, Qt
from PyQt5.QtGui import QColor, QIcon, QLinearGradient, QPainter, QPixmap
from PyQt5.QtWidgets import (QApplication, QCheckBox, QFormLayout, QFrame, QHBoxLayout, QLabel, QLineEdit,
                             QPushButton, QStackedWidget, QVBoxLayout, QWidget)

LOGO = "/usr/share/pixmaps/mikeos-amber.svg"
HELPER = os.environ.get("AMBER_CREATE_USER", "/usr/share/mikeos-amber/bin/amber-create-user")
DONE = "/var/lib/mikeos-amber/account-setup-done"
GUEST = "amber"

STYLE = """
QWidget#root { background: #f3e8ff; }
QLabel#headTitle { color: #1f1a1a; font-size: 20pt; font-weight: bold; }
QLabel#headText { color: #2a1000; font-size: 11pt; }
QLabel { color: #1c0a2e; }
QLabel#hint { color: #5d4878; font-size: 9pt; }
QLabel#error { color: #b3261e; font-weight: bold; }
QLabel#big { font-size: 15pt; font-weight: bold; color: #3a1f5c; }
QFrame#note { background: #fff4e0; border: 1px solid #f0b050; border-radius: 6px; }
QLineEdit { background: #ffffff; color: #1c0a2e; border: 1px solid #a98bc0; border-radius: 4px; padding: 5px;
            selection-background-color: #f08a00; selection-color: #1a0f00; font-size: 11pt; }
QLineEdit:focus { border: 2px solid #f08a00; }
QCheckBox { color: #1c0a2e; spacing: 8px; }
QPushButton { background: #fbf7ff; color: #1c0a2e; border: 1px solid #a98bc0; border-radius: 5px; padding: 6px 16px; }
QPushButton:hover { background: #fff3dc; }
QPushButton#primary { background: #f08a00; color: #1a0f00; border: 1px solid #b44a00; font-weight: bold; padding: 7px 22px; }
QPushButton#primary:hover { background: #ffa21a; }
QPushButton#primary:disabled { background: #e8d9c4; color: #8a7a6a; border-color: #d8c8b0; }
"""


def should_show():
    """Only on the amber account, only until the first account is made, and not on a USB stick that isn't saving."""
    if os.environ.get("AMBER_ACCOUNTS_TEST"):
        return True
    if os.path.exists(DONE):
        return False
    try:
        if pwd.getpwuid(os.getuid()).pw_name != GUEST:
            return False
        if "boot=live" in open("/proc/cmdline").read().split():
            # running from the USB: only worth it when changes are being saved to the stick
            if not any(" /run/live/persistence/" in ln for ln in open("/proc/mounts")):
                return False
    except (KeyError, OSError):
        return False
    return True


def suggest_username(full):
    first = full.strip().split(" ")[0] if full.strip() else ""
    u = re.sub(r"[^a-z0-9-]", "", first.lower())
    u = re.sub(r"^[^a-z]+", "", u)
    return u[:31]


class Header(QWidget):
    def __init__(self, title, text):
        super().__init__()
        self.setFixedHeight(120)
        self.logo = QPixmap(LOGO)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(140, 26, 20, 20)
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
            x = w - 130 + i * 20
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(c))
            p.drawPolygon(QPoint(x, h), QPoint(x + 14, h), QPoint(x + 14 + h, 0), QPoint(x + h, 0))
        p.fillRect(0, h - 4, w, 4, QColor("#7a12c4"))
        if not self.logo.isNull():
            p.drawPixmap(24, 12, self.logo.scaled(92, 92, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        p.end()


class Setup(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("root")
        self.setAttribute(Qt.WA_StyledBackground)
        self.setWindowTitle("Create Your Account")
        self.setWindowIcon(QIcon.fromTheme("mikeos-amber", QIcon(LOGO)))
        self.setStyleSheet(STYLE)
        self.setFixedWidth(560)
        self.user_edited = False
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        root.addWidget(Header("Create your account", "Make an account that's just for you."))
        self.pages = QStackedWidget()
        root.addWidget(self.pages)
        self.pages.addWidget(self.form_page())
        self.pages.addWidget(self.done_page())
        # wrapped text makes Qt guess the height too small, so work it out for the real width
        lay = self.layout()
        self.setMinimumHeight(max(lay.heightForWidth(560) if lay.hasHeightForWidth() else 0, 680))

    # ---------------------------------------------------------------- page 1
    def form_page(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(28, 18, 28, 18)
        lay.setSpacing(10)
        form = QFormLayout()
        form.setVerticalSpacing(8)
        form.setHorizontalSpacing(14)
        self.full = QLineEdit()
        self.full.setPlaceholderText("Like: Luke")
        self.full.setMaxLength(60)
        self.user = QLineEdit()
        self.user.setMaxLength(31)
        self.pw = QLineEdit()
        self.pw2 = QLineEdit()
        for w in (self.pw, self.pw2):
            w.setEchoMode(QLineEdit.Password)
        for w in (self.full, self.user, self.pw, self.pw2):
            w.setFixedHeight(max(32, w.sizeHint().height()))
        form.addRow("Your name:", self.full)
        form.addRow("User name:", self.user)
        hint = QLabel("Lowercase letters and numbers, no spaces.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        form.addRow("", hint)
        form.addRow("Password:", self.pw)
        form.addRow("Type it again:", self.pw2)
        self.show_pw = QCheckBox("Show password")
        self.show_pw.toggled.connect(lambda on: [w.setEchoMode(QLineEdit.Normal if on else QLineEdit.Password)
                                                 for w in (self.pw, self.pw2)])
        form.addRow("", self.show_pw)
        lay.addLayout(form)

        self.auto = QCheckBox("Sign in to my account automatically when the computer starts")
        self.auto.setChecked(True)
        self.admin = QCheckBox("Let my account install apps and change settings (administrator)")
        self.admin.setChecked(True)
        lay.addWidget(self.auto)
        auto_hint = QLabel("Only while yours is the only account. If more people get accounts, "
                           "the computer will show a list so everyone picks their own.")
        auto_hint.setObjectName("hint")
        auto_hint.setWordWrap(True)
        auto_hint.setContentsMargins(26, 0, 0, 4)
        lay.addWidget(auto_hint)
        lay.addWidget(self.admin)

        note = QFrame()
        note.setObjectName("note")
        nl = QVBoxLayout(note)
        nl.setContentsMargins(12, 8, 12, 8)
        t = QLabel("<b>The Amber account becomes Guest.</b> Anyone can use Guest without a password, but it "
                   "can't install apps or change settings. Your files stay in your own account.")
        t.setWordWrap(True)
        nl.addWidget(t)
        lay.addWidget(note)

        self.error = QLabel("")
        self.error.setObjectName("error")
        self.error.setWordWrap(True)
        lay.addWidget(self.error)

        row = QHBoxLayout()
        later = QPushButton("Not now")
        later.setToolTip("This will show up again next time")
        later.clicked.connect(self.close)
        row.addWidget(later)
        row.addStretch(1)
        self.go = QPushButton("Create account")
        self.go.setObjectName("primary")
        self.go.setDefault(True)
        self.go.clicked.connect(self.create)
        row.addWidget(self.go)
        lay.addLayout(row)

        self.full.textEdited.connect(self.name_changed)
        self.user.textEdited.connect(self.user_changed)
        for w in (self.full, self.user, self.pw, self.pw2):
            w.returnPressed.connect(self.create)
        return page

    def name_changed(self, text):
        if not self.user_edited:
            self.user.setText(suggest_username(text))

    def user_changed(self, text):
        self.user_edited = bool(text)
        clean = re.sub(r"[^a-z0-9-]", "", text.lower().replace(" ", ""))
        if clean != text:
            pos = self.user.cursorPosition()
            self.user.setText(clean)
            self.user.setCursorPosition(min(pos, len(clean)))

    def create(self):
        full, user, pw = " ".join(self.full.text().split()), self.user.text(), self.pw.text()
        if not full:
            return self.say("Type your name.", self.full)
        if not re.fullmatch(r"[a-z][a-z0-9-]{1,30}", user):
            return self.say("Your user name needs at least 2 letters or numbers, and has to start with a letter.", self.user)
        if user in ("amber", "guest", "root", "admin"):
            return self.say("That user name is taken. Try another one.", self.user)
        if len(pw) < 4:
            return self.say("Your password needs at least 4 characters.", self.pw)
        if pw != self.pw2.text():
            self.pw2.clear()
            return self.say("The two passwords don't match. Type it again.", self.pw2)
        self.error.setText("Creating your account...")
        self.go.setEnabled(False)
        self.made = (full, user)
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.finished.connect(self.created)
        if os.geteuid() == 0 or os.environ.get("AMBER_ACCOUNTS_TEST"):
            self.proc.start(HELPER, [])
        else:
            self.proc.start("pkexec", [HELPER])
        import json
        self.proc.write(json.dumps({"fullname": full, "username": user, "password": pw,
                                    "admin": self.admin.isChecked(), "autologin": self.auto.isChecked()}).encode())
        self.proc.closeWriteChannel()

    def say(self, text, focus=None):
        self.error.setText(text)
        if focus:
            focus.setFocus()

    def created(self, code, _status=0):
        out = bytes(self.proc.readAll()).decode(errors="replace")
        self.go.setEnabled(True)
        if code == 0 and "OK:" in out:
            self.done_name.setText("Hi, %s!" % self.made[0])
            self.done_text.setText(
                "Your account <b>%s</b> is ready.<br><br>Log out now and pick <b>%s</b> on the sign-in screen "
                "to start using it.%s" % (self.made[1], self.made[0],
                                          " From now on the computer will start in your account." if self.auto.isChecked() else ""))
            self.pages.setCurrentIndex(1)
            return
        msg = next((ln[6:] for ln in out.splitlines() if ln.startswith("ERROR:")), "")
        if not msg:
            msg = "Your account couldn't be made." if code not in (126, 127) else "That was cancelled."
        self.error.setText(msg)

    # ---------------------------------------------------------------- page 2
    def done_page(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(28, 24, 28, 18)
        self.done_name = QLabel("")
        self.done_name.setObjectName("big")
        lay.addWidget(self.done_name)
        self.done_text = QLabel("")
        self.done_text.setWordWrap(True)
        lay.addWidget(self.done_text)
        lay.addStretch(1)
        row = QHBoxLayout()
        later = QPushButton("Later")
        later.clicked.connect(self.close)
        row.addWidget(later)
        row.addStretch(1)
        out = QPushButton("Log out now")
        out.setObjectName("primary")
        out.clicked.connect(self.log_out)
        row.addWidget(out)
        lay.addLayout(row)
        return page

    def log_out(self):
        self.close()
        if os.environ.get("AMBER_ACCOUNTS_TEST"):
            return
        desk = os.environ.get("XDG_CURRENT_DESKTOP", "")
        tries = []
        if "LXQt" in desk and shutil.which("lxqt-leave"):
            tries.append(["lxqt-leave", "--logout"])
        if "KDE" in desk:
            for q in ("qdbus", "qdbus-qt5"):
                if shutil.which(q):
                    tries.append([q, "org.kde.ksmserver", "/KSMServer", "logout", "0", "0", "0"])
        sid = os.environ.get("XDG_SESSION_ID")
        tries.append(["loginctl", "terminate-session", sid] if sid else ["loginctl", "terminate-user", GUEST])
        for cmd in tries:
            try:
                if subprocess.run(cmd, timeout=15).returncode == 0:
                    return
            except (OSError, subprocess.TimeoutExpired):
                pass


def light_palette(app):
    from PyQt5.QtGui import QPalette
    if app.palette().color(QPalette.Window).lightness() < 128:
        pal = QPalette()
        for role, col in ((QPalette.Window, "#f3e8ff"), (QPalette.WindowText, "#1c0a2e"), (QPalette.Base, "#ffffff"),
                          (QPalette.Text, "#1c0a2e"), (QPalette.Button, "#fbf7ff"), (QPalette.ButtonText, "#1c0a2e"),
                          (QPalette.Highlight, "#f08a00"), (QPalette.HighlightedText, "#1a0f00"),
                          (QPalette.ToolTipBase, "#fbf7ff"), (QPalette.ToolTipText, "#1c0a2e"),
                          (QPalette.PlaceholderText, "#8a7a9a")):
            pal.setColor(role, QColor(col))
        app.setPalette(pal)


def main():
    if "--autostart" in sys.argv and not should_show():
        return
    app = QApplication(sys.argv)
    app.setDesktopFileName("mikeos-amber-account-setup")
    light_palette(app)
    w = Setup()
    w.show()
    w.raise_()
    w.activateWindow()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
