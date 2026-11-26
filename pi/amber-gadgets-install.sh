#!/bin/bash
# Mike OS: Amber - installs the Gadgets sidebar on an existing Mike OS install (Pi or PC).
# Run:   sudo bash amber-gadgets-install.sh
# Gadgets: clock, calendar, CPU meter, disk space, sticky notes with reminders, weather,
# picture puzzle, slide show. Right-click a gadget for options and the gadget list.

if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
APP=/usr/share/mikeos-amber/apps/gadgets.py
echo "=== Installing Mike OS: Amber Gadgets ==="
mkdir -p /usr/share/mikeos-amber/apps /usr/share/applications /etc/xdg/autostart
cat > "$APP" <<'GADGETS_PY_EOF'
#!/usr/bin/env python3
"""Gadgets - Mike OS: Amber. A Windows 7-style gadget column on the right of the desktop.

Right-click any gadget for the gadget list and options. Settings and notes are kept in
~/.config/mikeos-amber-gadgets.json and ~/.local/share/mikeos-amber/notes.json.
"""
import calendar
import datetime as dt
import fcntl
import glob
import json
import math
import os
import random
import shutil
import subprocess
import sys
import threading
import urllib.parse
import urllib.request

from PyQt5.QtCore import Qt, QTimer, QRect, QRectF, QPointF, QDateTime, pyqtSignal, QObject
from PyQt5.QtGui import (QBitmap, QColor, QFont, QImage, QLinearGradient, QPainter, QPainterPath,
                         QPen, QPixmap, QRadialGradient)
from PyQt5.QtWidgets import (QAction, QApplication, QCheckBox, QDateTimeEdit, QDialog, QHBoxLayout,
                             QInputDialog, QMenu, QMessageBox, QPushButton, QTextEdit, QVBoxLayout,
                             QWidget)

HOME = os.path.expanduser("~")
CONF = os.path.join(HOME, ".config", "mikeos-amber-gadgets.json")
NOTES = os.path.join(HOME, ".local", "share", "mikeos-amber", "notes.json")
LOCK = os.path.join(HOME, ".cache", "mikeos-amber-gadgets.lock")
LOGO = "/usr/share/pixmaps/mikeos-amber.svg"
SOUNDS = "/usr/share/sounds/mikeos-amber"

W = 196            # gadget width
GAP = 10           # space between gadgets
EDGE = 12          # space from the screen edge
RADIUS = 10

# Amber colors
PURPLE_DARK = QColor("#2a1244")
PURPLE = QColor("#3a1f5c")
PURPLE_HI = QColor("#7a12c4")
ORANGE = QColor("#f08a00")
ORANGE_HI = QColor("#ffb629")
GREEN = QColor("#1e9e3a")
BLUE = QColor("#1560d8")
CREAM = QColor("#fff8e6")
DIM = QColor("#c9b8e0")
PAPER = QColor("#ffe08a")

DEFAULTS = {"enabled": ["clock", "calendar", "cpu", "disk", "notes", "weather"],
            "autostart": True, "city": "", "lat": None, "lon": None, "fahrenheit": True}


# ------------------------------------------------------------------ helpers
def load_json(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path, data):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=1)
        os.replace(tmp, path)
    except OSError:
        pass


def play(name):
    player = shutil.which("pw-play") or shutil.which("paplay")
    path = os.path.join(SOUNDS, name + ".ogg")
    if player and os.path.exists(path):
        try:
            subprocess.Popen([player, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass


def open_path(path):
    for cmd in (["dolphin", path], ["xdg-open", path]):
        if shutil.which(cmd[0]):
            subprocess.Popen(cmd, start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return


def font(size, bold=False):
    f = QFont("DejaVu Sans", size)
    f.setBold(bold)
    return f


def human(nbytes):
    gb = nbytes / 1e9
    return "%.0f GB" % gb if gb >= 10 else "%.1f GB" % gb


def x11_window_states(win_id, states):
    """Tell the window manager to keep this window off the taskbar/pager and below other windows.
    Set before the window is shown, so every window manager (KWin, Openbox) picks it up."""
    try:
        import ctypes
        import ctypes.util
        x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library("X11") or "libX11.so.6")
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XInternAtom.restype = ctypes.c_ulong
        x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x11.XChangeProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong,
                                        ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        x11.XFlush.argtypes = [ctypes.c_void_p]
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        d = x11.XOpenDisplay(None)
        if not d:
            return
        atoms = (ctypes.c_ulong * len(states))(*[x11.XInternAtom(d, a.encode(), 0) for a in states])
        x11.XChangeProperty(d, int(win_id), x11.XInternAtom(d, b"_NET_WM_STATE", 0), 4, 32, 0,
                            ctypes.cast(atoms, ctypes.c_void_p), len(states))
        x11.XFlush(d)
        x11.XCloseDisplay(d)
    except Exception:
        pass


# ------------------------------------------------------------------ base gadget
class Gadget(QWidget):
    key = ""
    title = ""
    height_hint = 120
    resized = pyqtSignal()

    def __init__(self, host):
        super().__init__(host)
        self.host = host
        self.resize(W, self.height_hint)

    def menu_actions(self, menu):
        """Gadget-specific entries for the right-click menu."""

    def paint_card(self, p, title=None):
        r = QRectF(0.5, 0.5, self.width() - 1, self.height() - 1)
        path = QPainterPath()
        path.addRoundedRect(r, RADIUS, RADIUS)
        g = QLinearGradient(0, 0, 0, self.height())
        g.setColorAt(0, PURPLE)
        g.setColorAt(1, PURPLE_DARK)
        p.fillPath(path, g)
        p.setPen(QPen(QColor(255, 255, 255, 70), 1))
        p.drawPath(path)
        if title:
            p.setPen(ORANGE_HI)
            p.setFont(font(8, True))
            p.drawText(QRect(12, 6, self.width() - 24, 16), Qt.AlignLeft | Qt.AlignVCenter, title.upper())

    def contextMenuEvent(self, e):
        self.host.show_menu(self, e.globalPos())


# ------------------------------------------------------------------ clock
class ClockGadget(Gadget):
    key, title, height_hint = "clock", "Clock", 196

    def __init__(self, host):
        super().__init__(host)
        t = QTimer(self)
        t.timeout.connect(self.update)
        t.start(1000)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p)
        now = dt.datetime.now()
        c = QPointF(self.width() / 2, 88)
        r = 72
        g = QRadialGradient(c, r)
        g.setColorAt(0, QColor("#fff3dc"))
        g.setColorAt(0.85, CREAM)
        g.setColorAt(1, QColor("#ffd98a"))
        p.setPen(QPen(ORANGE, 5))
        p.setBrush(g)
        p.drawEllipse(c, r, r)
        for i in range(60):                      # ticks
            a = math.radians(i * 6)
            outer, inner = r - 6, (r - 15 if i % 5 == 0 else r - 10)
            p.setPen(QPen(PURPLE if i % 5 == 0 else DIM, 3 if i % 5 == 0 else 1))
            p.drawLine(QPointF(c.x() + inner * math.sin(a), c.y() - inner * math.cos(a)),
                       QPointF(c.x() + outer * math.sin(a), c.y() - outer * math.cos(a)))

        def hand(angle, length, width, color):
            a = math.radians(angle)
            p.setPen(QPen(color, width, Qt.SolidLine, Qt.RoundCap))
            p.drawLine(QPointF(c.x() - 8 * math.sin(a), c.y() + 8 * math.cos(a)),
                       QPointF(c.x() + length * math.sin(a), c.y() - length * math.cos(a)))
        hand((now.hour % 12 + now.minute / 60) * 30, 38, 6, PURPLE)
        hand((now.minute + now.second / 60) * 6, 56, 4, PURPLE_HI)
        hand(now.second * 6, 60, 2, ORANGE)
        p.setBrush(ORANGE)
        p.setPen(Qt.NoPen)
        p.drawEllipse(c, 5, 5)
        p.setPen(CREAM)
        p.setFont(font(11, True))
        p.drawText(QRect(0, 166, self.width(), 22), Qt.AlignCenter,
                   now.strftime("%A  %I:%M %p").replace(" 0", " "))
        p.end()


# ------------------------------------------------------------------ calendar
class CalendarGadget(Gadget):
    key, title, height_hint = "calendar", "Calendar", 150

    def __init__(self, host):
        super().__init__(host)
        self.month_view = False
        self.day = dt.date.today()
        t = QTimer(self)
        t.timeout.connect(self.check_day)
        t.start(30000)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("Click to switch between today and the whole month")

    def check_day(self):
        if dt.date.today() != self.day:
            self.day = dt.date.today()
            self.update()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.month_view = not self.month_view
            self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p)
        today = dt.date.today()
        if not self.month_view:
            page = QRectF(26, 12, self.width() - 52, self.height() - 24)
            path = QPainterPath()
            path.addRoundedRect(page, 6, 6)
            p.fillPath(path, CREAM)
            top = QPainterPath()
            top.addRoundedRect(QRectF(page.x(), page.y(), page.width(), 30), 6, 6)
            p.fillPath(top, ORANGE)
            p.fillRect(QRectF(page.x(), page.y() + 20, page.width(), 10), ORANGE)
            p.setPen(QColor("#1f1a1a"))
            p.setFont(font(11, True))
            p.drawText(QRectF(page.x(), page.y(), page.width(), 30), Qt.AlignCenter, today.strftime("%B %Y"))
            p.setPen(PURPLE)
            p.setFont(font(44, True))
            p.drawText(QRectF(page.x(), page.y() + 28, page.width(), 62), Qt.AlignCenter, str(today.day))
            p.setPen(PURPLE_HI)
            p.setFont(font(11, True))
            p.drawText(QRectF(page.x(), page.y() + 90, page.width(), 24), Qt.AlignCenter, today.strftime("%A"))
        else:
            p.setPen(ORANGE_HI)
            p.setFont(font(10, True))
            p.drawText(QRect(0, 8, self.width(), 18), Qt.AlignCenter, today.strftime("%B %Y"))
            cw, top = (self.width() - 20) / 7, 30
            p.setFont(font(7, True))
            p.setPen(DIM)
            for i, d in enumerate("SMTWTFS"):
                p.drawText(QRectF(10 + i * cw, top, cw, 14), Qt.AlignCenter, d)
            cal = calendar.Calendar(firstweekday=6).monthdayscalendar(today.year, today.month)
            p.setFont(font(8))
            for row, week in enumerate(cal):
                for col, d in enumerate(week):
                    if not d:
                        continue
                    cell = QRectF(10 + col * cw, top + 15 + row * 17, cw, 16)
                    if d == today.day:
                        p.setBrush(ORANGE)
                        p.setPen(Qt.NoPen)
                        p.drawRoundedRect(cell.adjusted(2, 0, -2, 0), 4, 4)
                        p.setPen(QColor("#1a0f00"))
                    else:
                        p.setPen(CREAM)
                    p.drawText(cell, Qt.AlignCenter, str(d))
        p.end()


# ------------------------------------------------------------------ CPU meter
def read_cpu():
    try:
        with open("/proc/stat") as f:
            v = [int(x) for x in f.readline().split()[1:]]
        idle = v[3] + (v[4] if len(v) > 4 else 0)
        return idle, sum(v)
    except (OSError, ValueError, IndexError):
        return 0, 0


def read_mem():
    info = {}
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                info[k] = int(v.split()[0])
        total, avail = info["MemTotal"], info.get("MemAvailable", info.get("MemFree", 0))
        return 100.0 * (total - avail) / total, total * 1024
    except (OSError, ValueError, KeyError, ZeroDivisionError):
        return 0.0, 0


class CpuGadget(Gadget):
    key, title, height_hint = "cpu", "CPU Meter", 128

    def __init__(self, host):
        super().__init__(host)
        self.last = read_cpu()
        self.cpu = 0.0
        self.mem, self.mem_total = read_mem()
        t = QTimer(self)
        t.timeout.connect(self.sample)
        t.start(2000)

    def sample(self):
        idle, total = read_cpu()
        d_idle, d_total = idle - self.last[0], total - self.last[1]
        self.last = (idle, total)
        if d_total > 0:
            self.cpu = max(0.0, min(100.0, 100.0 * (1 - d_idle / d_total)))
        self.mem, self.mem_total = read_mem()
        self.update()

    def dial(self, p, cx, cy, r, value, label, color):
        p.setPen(QPen(QColor("#1c0a2e"), 1))
        p.setBrush(QColor("#1c0a2e"))
        p.drawEllipse(QPointF(cx, cy), r, r)
        p.setPen(QPen(QColor(255, 255, 255, 40), 6, Qt.SolidLine, Qt.FlatCap))
        arc = QRectF(cx - r + 7, cy - r + 7, 2 * r - 14, 2 * r - 14)
        p.drawArc(arc, 225 * 16, -270 * 16)
        p.setPen(QPen(color, 6, Qt.SolidLine, Qt.FlatCap))
        p.drawArc(arc, 225 * 16, int(-270 * 16 * value / 100))
        a = math.radians(225 - 270 * value / 100)
        p.setPen(QPen(CREAM, 2, Qt.SolidLine, Qt.RoundCap))
        p.drawLine(QPointF(cx, cy), QPointF(cx + (r - 14) * math.cos(a), cy - (r - 14) * math.sin(a)))
        p.setBrush(color)
        p.setPen(Qt.NoPen)
        p.drawEllipse(QPointF(cx, cy), 4, 4)
        p.setPen(CREAM)
        p.setFont(font(10, True))
        p.drawText(QRectF(cx - r, cy + r * 0.35, 2 * r, 16), Qt.AlignCenter, "%d%%" % round(value))
        p.setPen(DIM)
        p.setFont(font(8, True))
        p.drawText(QRectF(cx - r, cy + r + 2, 2 * r, 14), Qt.AlignCenter, label)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        self.dial(p, 52, 64, 38, self.cpu, "CPU", ORANGE)
        self.dial(p, self.width() - 52, 64, 38, self.mem, "MEMORY", GREEN)
        p.end()


# ------------------------------------------------------------------ disk space
def list_drives():
    """[(name, mount point, free bytes, total bytes)] for the system and plugged-in drives."""
    out, seen = [], set()
    mounts = [("This computer", "/")]
    try:
        with open("/proc/mounts") as f:
            for line in f:
                parts = line.split()
                if len(parts) > 1 and parts[1].startswith(("/media/", "/run/media/")):
                    mp = parts[1].replace("\\040", " ")
                    mounts.append((os.path.basename(mp) or mp, mp))
    except OSError:
        pass
    for name, mp in mounts:
        try:
            st = os.statvfs(mp)
        except OSError:
            continue
        key = (st.f_blocks, st.f_bfree, st.f_fsid if hasattr(st, "f_fsid") else mp)
        if key in seen or st.f_blocks * st.f_frsize < 64 * 1024 * 1024:
            continue
        seen.add(key)
        out.append((name, mp, st.f_bavail * st.f_frsize, st.f_blocks * st.f_frsize))
    return out[:4]


class DiskGadget(Gadget):
    key, title = "disk", "Disk Space"

    def __init__(self, host):
        super().__init__(host)
        self.drives = []
        self.refresh(initial=True)
        t = QTimer(self)
        t.timeout.connect(self.refresh)
        t.start(20000)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("Click a drive to open it")

    def refresh(self, initial=False):
        drives = list_drives()
        changed = len(drives) != len(self.drives)
        self.drives = drives
        h = 30 + 42 * max(1, len(drives))
        if h != self.height():
            self.resize(W, h)
            if not initial:
                self.resized.emit()
        elif changed:
            self.resized.emit()
        self.update()

    def mouseReleaseEvent(self, e):
        i = (e.pos().y() - 26) // 42
        if e.button() == Qt.LeftButton and 0 <= i < len(self.drives):
            open_path(self.drives[i][1])

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        for i, (name, _mp, free, total) in enumerate(self.drives):
            y = 26 + i * 42
            used = 1 - free / total if total else 0
            p.setPen(CREAM)
            p.setFont(font(9, True))
            p.drawText(QRect(12, y, self.width() - 24, 16), Qt.AlignLeft | Qt.AlignVCenter, name)
            bar = QRectF(12, y + 18, self.width() - 24, 9)
            p.setPen(Qt.NoPen)
            p.setBrush(QColor("#1c0a2e"))
            p.drawRoundedRect(bar, 4, 4)
            p.setBrush(QColor("#c62828") if used > 0.9 else ORANGE)
            p.drawRoundedRect(QRectF(bar.x(), bar.y(), max(8, bar.width() * used), bar.height()), 4, 4)
            p.setPen(DIM)
            p.setFont(font(7))
            p.drawText(QRect(12, y + 28, self.width() - 24, 12), Qt.AlignLeft | Qt.AlignVCenter,
                       "%s free of %s" % (human(free), human(total)))
        if not self.drives:
            p.setPen(DIM)
            p.drawText(self.rect(), Qt.AlignCenter, "No drives found")
        p.end()


# ------------------------------------------------------------------ sticky notes + reminders
class NoteDialog(QDialog):
    def __init__(self, parent, note=None):
        super().__init__(None)
        self.setWindowTitle("Sticky Note")
        self.setWindowFlag(Qt.WindowStaysOnTopHint, True)
        self.deleted = False
        lay = QVBoxLayout(self)
        self.text = QTextEdit()
        self.text.setPlaceholderText("Write your note here...")
        self.text.setStyleSheet("QTextEdit { background: #ffe08a; color: #1c0a2e; font-size: 11pt; }")
        self.text.setPlainText(note["text"] if note else "")
        lay.addWidget(self.text)
        row = QHBoxLayout()
        self.remind = QCheckBox("Remind me at")
        self.when = QDateTimeEdit()
        self.when.setCalendarPopup(True)
        self.when.setDisplayFormat("ddd MMM d yyyy  h:mm AP")
        if note and note.get("remind_at"):
            self.remind.setChecked(True)
            self.when.setDateTime(QDateTime.fromSecsSinceEpoch(int(note["remind_at"])))
        else:
            self.when.setDateTime(QDateTime.currentDateTime().addSecs(3600))
        self.when.setEnabled(self.remind.isChecked())
        self.remind.toggled.connect(self.when.setEnabled)
        row.addWidget(self.remind)
        row.addWidget(self.when, 1)
        lay.addLayout(row)
        buttons = QHBoxLayout()
        if note:
            delete = QPushButton("Delete note")
            delete.clicked.connect(self.delete)
            buttons.addWidget(delete)
        buttons.addStretch(1)
        cancel = QPushButton("Cancel")
        cancel.clicked.connect(self.reject)
        save = QPushButton("Save")
        save.setDefault(True)
        save.clicked.connect(self.accept)
        buttons.addWidget(cancel)
        buttons.addWidget(save)
        lay.addLayout(buttons)
        self.resize(380, 260)

    def delete(self):
        self.deleted = True
        self.accept()

    def result_note(self):
        text = self.text.toPlainText().strip()
        remind = self.when.dateTime().toSecsSinceEpoch() if self.remind.isChecked() else None
        return {"text": text, "remind_at": remind, "fired": False}


def due_notes(notes, now):
    """Indexes of notes whose reminder time has come and that haven't gone off yet."""
    return [i for i, n in enumerate(notes)
            if n.get("remind_at") and not n.get("fired") and n["remind_at"] <= now]


class NotesGadget(Gadget):
    key, title = "notes", "Sticky Notes"
    ROW = 46

    def __init__(self, host):
        super().__init__(host)
        self.notes = load_json(NOTES, [])
        self.relayout(initial=True)
        t = QTimer(self)
        t.timeout.connect(self.check_reminders)
        t.start(15000)
        QTimer.singleShot(3000, self.check_reminders)
        self.setCursor(Qt.PointingHandCursor)

    def relayout(self, initial=False):
        h = 30 + self.ROW * len(self.notes) + 34
        if h != self.height():
            self.resize(W, h)
            if not initial:
                self.resized.emit()
        self.update()

    def save(self):
        save_json(NOTES, self.notes)
        self.relayout()

    def menu_actions(self, menu):
        menu.addAction("New sticky note...", self.new_note)

    def new_note(self):
        d = NoteDialog(self.window())
        if d.exec_() == QDialog.Accepted:
            n = d.result_note()
            if n["text"]:
                self.notes.append(n)
                self.save()

    def edit_note(self, i):
        d = NoteDialog(self.window(), self.notes[i])
        if d.exec_() == QDialog.Accepted:
            if d.deleted:
                del self.notes[i]
            else:
                n = d.result_note()
                if n["text"]:
                    self.notes[i] = n
                else:
                    del self.notes[i]
            self.save()

    def mouseReleaseEvent(self, e):
        if e.button() != Qt.LeftButton:
            return
        y = e.pos().y()
        i = (y - 26) // self.ROW
        if y >= self.height() - 32:
            self.new_note()
        elif 0 <= i < len(self.notes) and y >= 26:
            self.edit_note(i)

    def check_reminders(self):
        now = dt.datetime.now().timestamp()
        due = due_notes(self.notes, now)
        for i in due:
            self.notes[i]["fired"] = True
        if due:
            save_json(NOTES, self.notes)
            self.update()
            play("warning")
            for i in due:
                self.host.notify("Reminder", self.notes[i]["text"])

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        now = dt.datetime.now().timestamp()
        for i, n in enumerate(self.notes):
            r = QRectF(10, 26 + i * self.ROW, self.width() - 20, self.ROW - 6)
            due = n.get("remind_at") and n["remind_at"] <= now
            p.setPen(Qt.NoPen)
            p.setBrush(QColor("#ffb629") if due else PAPER)
            p.drawRoundedRect(r, 4, 4)
            p.setPen(QColor("#1c0a2e"))
            p.setFont(font(8))
            text = n["text"].replace("\n", " ")
            fm = p.fontMetrics()
            line_h = 14 if n.get("remind_at") else 30
            p.drawText(r.adjusted(6, 3, -6, -(r.height() - 3 - line_h)), Qt.AlignLeft | Qt.TextWordWrap,
                       fm.elidedText(text, Qt.ElideRight, int(r.width() - 12) * (1 if n.get("remind_at") else 2)))
            if n.get("remind_at"):
                when = dt.datetime.fromtimestamp(n["remind_at"])
                label = ("Due " if due else "") + when.strftime("%a %b %d, %I:%M %p").replace(" 0", " ")
                p.setPen(QColor("#7a12c4") if not due else QColor("#7a0000"))
                p.setFont(font(7, True))
                p.drawText(r.adjusted(6, 20, -6, -2), Qt.AlignLeft | Qt.AlignVCenter, "⏰ " + label)
        add = QRectF(10, self.height() - 32, self.width() - 20, 24)
        p.setPen(QPen(ORANGE, 1, Qt.DashLine))
        p.setBrush(Qt.NoBrush)
        p.drawRoundedRect(add, 4, 4)
        p.setPen(ORANGE_HI)
        p.setFont(font(9, True))
        p.drawText(add, Qt.AlignCenter, "+  New note")
        p.end()


# ------------------------------------------------------------------ weather
WMO = {0: ("Clear", "sun"), 1: ("Mostly clear", "sun"), 2: ("Partly cloudy", "partly"), 3: ("Cloudy", "cloud"),
       45: ("Fog", "cloud"), 48: ("Fog", "cloud"), 51: ("Drizzle", "rain"), 53: ("Drizzle", "rain"),
       55: ("Drizzle", "rain"), 56: ("Freezing drizzle", "rain"), 57: ("Freezing drizzle", "rain"),
       61: ("Light rain", "rain"), 63: ("Rain", "rain"), 65: ("Heavy rain", "rain"),
       66: ("Freezing rain", "rain"), 67: ("Freezing rain", "rain"), 71: ("Light snow", "snow"),
       73: ("Snow", "snow"), 75: ("Heavy snow", "snow"), 77: ("Snow grains", "snow"),
       80: ("Showers", "rain"), 81: ("Showers", "rain"), 82: ("Heavy showers", "rain"),
       85: ("Snow showers", "snow"), 86: ("Snow showers", "snow"), 95: ("Thunderstorm", "storm"),
       96: ("Thunderstorm", "storm"), 99: ("Thunderstorm", "storm")}


def parse_forecast(data):
    cur = data["current"]
    daily = data["daily"]
    text, icon = WMO.get(int(cur["weather_code"]), ("", "cloud"))
    return {"temp": round(cur["temperature_2m"]), "text": text, "icon": icon,
            "high": round(daily["temperature_2m_max"][0]), "low": round(daily["temperature_2m_min"][0])}


# Mike Weather writes this while its window is open in recording mode, so the gadget hides the city too.
RECORD_FLAG = os.path.join(HOME, ".cache", "mikeos-amber-recording.json")


def mike_weather_recording():
    """None if Mike Weather isn't recording, otherwise whether to still show the state."""
    d = load_json(RECORD_FLAG, None)
    if not isinstance(d, dict) or not os.path.exists("/proc/%s" % d.get("pid", 0)):
        return None                             # no file, or Mike Weather closed without cleaning up
    return bool(d.get("state", True))


def fetch_json(url, timeout=15):
    req = urllib.request.Request(url, headers={"User-Agent": "MikeOS-Amber-Gadgets/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


class WeatherSignals(QObject):
    done = pyqtSignal(object)


class WeatherGadget(Gadget):
    key, title, height_hint = "weather", "Weather", 112

    def __init__(self, host):
        super().__init__(host)
        self.data = None
        self.status = ""
        self.sig = WeatherSignals()
        self.sig.done.connect(self.got)
        t = QTimer(self)
        t.timeout.connect(self.refresh)
        t.start(30 * 60 * 1000)
        QTimer.singleShot(4000, self.refresh)
        self.rec_now = self.recording()
        rt = QTimer(self)
        rt.timeout.connect(self.check_recording)
        rt.start(1500)

    def recording(self):
        """None = show the location; True/False = hide it, still showing the state or not."""
        mw = mike_weather_recording()
        if mw is not None:
            return mw
        if self.host.conf.get("recording", False):
            return self.host.conf.get("rec_state", True)
        return None

    def check_recording(self):
        now = self.recording()
        if now != self.rec_now:
            self.rec_now = now
            self.update()

    def menu_actions(self, menu):
        menu.addAction("Set weather city...", self.set_city)
        a = menu.addAction("Use °F (Fahrenheit)")
        a.setCheckable(True)
        a.setChecked(self.host.conf.get("fahrenheit", True))
        a.toggled.connect(self.set_units)
        a = menu.addAction("Recording mode (hide location)")
        a.setCheckable(True)
        a.setChecked(self.host.conf.get("recording", False))
        a.toggled.connect(lambda on: self.set_rec("recording", on))
        b = menu.addAction("    Still show my state")
        b.setCheckable(True)
        b.setChecked(self.host.conf.get("rec_state", True))
        b.setEnabled(self.host.conf.get("recording", False))
        b.toggled.connect(lambda on: self.set_rec("rec_state", on))

    def set_rec(self, key, on):
        self.host.conf[key] = on
        self.host.save_conf()
        self.check_recording()

    def set_units(self, on):
        self.host.conf["fahrenheit"] = on
        self.host.save_conf()
        self.refresh()

    def set_city(self):
        city, ok = QInputDialog.getText(None, "Weather", "Your city (for example: Denver):",
                                        text=self.host.conf.get("city", ""))
        if ok and city.strip():
            self.host.conf.update({"city": city.strip(), "lat": None, "lon": None})
            self.host.save_conf()
            self.status = "Looking up %s..." % city.strip()
            self.update()
            self.refresh()

    def refresh(self):
        conf = dict(self.host.conf)
        if not conf.get("city"):
            self.status = "Right-click to set your city"
            self.update()
            return
        threading.Thread(target=self.worker, args=(conf,), daemon=True).start()

    def worker(self, conf):
        try:
            lat, lon, name = conf.get("lat"), conf.get("lon"), conf["city"]
            if lat is None:
                g = fetch_json("https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&name="
                               + urllib.parse.quote(conf["city"]))
                if not g.get("results"):
                    self.sig.done.emit({"error": "City not found"})
                    return
                r = g["results"][0]
                lat, lon = r["latitude"], r["longitude"]
                name = r["name"] + (", " + r["admin1"] if r.get("admin1") else "")
            unit = "fahrenheit" if conf.get("fahrenheit", True) else "celsius"
            f = fetch_json("https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                           "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                           "&forecast_days=1&timezone=auto&temperature_unit=%s" % (lat, lon, unit))
            out = parse_forecast(f)
            out.update({"lat": lat, "lon": lon, "name": name})
            self.sig.done.emit(out)
        except Exception:                       # no internet, server down, odd reply
            self.sig.done.emit({"error": "No internet connection"})

    def got(self, d):
        if "error" in d:
            self.status = d["error"]
            if self.data is None:
                self.update()
            return
        self.host.conf.update({"lat": d["lat"], "lon": d["lon"], "city_name": d["name"]})
        self.host.save_conf()
        self.data, self.status = d, ""
        self.update()

    def draw_icon(self, p, kind, x, y):
        p.setPen(Qt.NoPen)
        if kind in ("sun", "partly"):
            p.setBrush(ORANGE_HI)
            p.drawEllipse(QPointF(x + 22, y + 20), 15, 15)
            p.setPen(QPen(ORANGE_HI, 3, Qt.SolidLine, Qt.RoundCap))
            for i in range(8):
                a = math.radians(i * 45)
                p.drawLine(QPointF(x + 22 + 19 * math.cos(a), y + 20 + 19 * math.sin(a)),
                           QPointF(x + 22 + 25 * math.cos(a), y + 20 + 25 * math.sin(a)))
            p.setPen(Qt.NoPen)
        if kind != "sun":
            p.setBrush(QColor("#e8e0f0") if kind != "storm" else QColor("#8a80a0"))
            ox = 8 if kind == "partly" else 0
            for cx, cy, r in ((14, 32, 11), (28, 26, 14), (42, 32, 10)):
                p.drawEllipse(QPointF(x + cx + ox, y + cy), r, r)
            p.drawRect(QRectF(x + 14 + ox, y + 32, 28, 10))
        if kind == "rain":
            p.setPen(QPen(BLUE.lighter(140), 2, Qt.SolidLine, Qt.RoundCap))
            for i in range(4):
                p.drawLine(QPointF(x + 12 + i * 9, y + 46), QPointF(x + 9 + i * 9, y + 54))
        elif kind == "snow":
            p.setBrush(CREAM)
            p.setPen(Qt.NoPen)
            for i in range(4):
                p.drawEllipse(QPointF(x + 12 + i * 9, y + 50), 2.5, 2.5)
        elif kind == "storm":
            p.setBrush(ORANGE_HI)
            p.setPen(Qt.NoPen)
            p.drawPolygon(QPointF(x + 26, y + 42), QPointF(x + 20, y + 54), QPointF(x + 26, y + 52),
                          QPointF(x + 23, y + 62), QPointF(x + 33, y + 48), QPointF(x + 27, y + 49))

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        if self.data:
            d = self.data
            self.draw_icon(p, d["icon"], 12, 26)
            unit = "°"
            p.setPen(CREAM)
            p.setFont(font(26, True))
            p.drawText(QRect(80, 22, self.width() - 88, 44), Qt.AlignRight | Qt.AlignVCenter, "%d%s" % (d["temp"], unit))
            p.setFont(font(8, True))
            p.setPen(ORANGE_HI)
            p.drawText(QRect(80, 62, self.width() - 88, 14), Qt.AlignRight, "H %d%s  L %d%s" % (d["high"], unit, d["low"], unit))
            p.setPen(DIM)
            p.setFont(font(8))
            p.drawText(QRect(12, 80, self.width() - 24, 14), Qt.AlignLeft, d["text"])
            name = d["name"]
            if self.rec_now is not None:           # recording: the state at most
                name = name.rsplit(",", 1)[1].strip() if self.rec_now and "," in name else ""
            p.drawText(QRect(12, 94, self.width() - 24, 14), Qt.AlignLeft,
                       p.fontMetrics().elidedText(name, Qt.ElideRight, self.width() - 24))
        else:
            p.setPen(DIM)
            p.setFont(font(9))
            p.drawText(QRect(12, 24, self.width() - 24, self.height() - 30), Qt.AlignCenter | Qt.TextWordWrap,
                       ("Loading weather..." if self.rec_now is not None and self.status.startswith("Looking up")
                        else self.status or "Loading weather..."))
        p.end()


# ------------------------------------------------------------------ picture puzzle
class PuzzleBoard:
    """4x4 sliding puzzle. tiles[i] is the piece at position i; 15 is the gap."""

    def __init__(self, rng=None):
        self.rng = rng or random.Random()
        self.tiles = list(range(16))
        self.shuffle()

    def solved(self):
        return self.tiles == list(range(16))

    def neighbors(self, pos):
        r, c = divmod(pos, 4)
        return [(r + dr) * 4 + c + dc for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1))
                if 0 <= r + dr < 4 and 0 <= c + dc < 4]

    def move(self, pos):
        gap = self.tiles.index(15)
        if pos in self.neighbors(gap):
            self.tiles[gap], self.tiles[pos] = self.tiles[pos], self.tiles[gap]
            return True
        return False

    def shuffle(self, steps=200):
        self.tiles = list(range(16))
        prev = None
        for _ in range(steps):                      # random legal moves keep it solvable
            gap = self.tiles.index(15)
            options = [n for n in self.neighbors(gap) if n != prev]
            pick = self.rng.choice(options)
            prev = gap
            self.move(pick)
        if self.solved():
            self.shuffle(steps)


class PuzzleGadget(Gadget):
    key, title, height_hint = "puzzle", "Picture Puzzle", 214

    def __init__(self, host):
        super().__init__(host)
        self.board = PuzzleBoard()
        self.picture = QPixmap(LOGO)
        if self.picture.isNull():
            img = QImage(160, 160, QImage.Format_RGB32)
            img.fill(ORANGE)
            self.picture = QPixmap.fromImage(img)
        bg = QPixmap(160, 160)
        bg.fill(ORANGE)
        pp = QPainter(bg)
        pp.setRenderHint(QPainter.SmoothPixmapTransform)
        pp.drawPixmap(QRect(4, 4, 152, 152), self.picture)
        pp.end()
        self.picture = bg
        self.setCursor(Qt.PointingHandCursor)

    def menu_actions(self, menu):
        menu.addAction("Shuffle the puzzle", self.reshuffle)

    def reshuffle(self):
        self.board.shuffle()
        self.update()

    def area(self):
        return QRect((self.width() - 160) // 2, 26, 160, 160)

    def mouseReleaseEvent(self, e):
        a = self.area()
        if e.button() != Qt.LeftButton:
            return
        if not a.contains(e.pos()):
            if e.pos().y() > a.bottom() and self.board.solved():
                self.reshuffle()
            return
        c, r = (e.pos().x() - a.x()) // 40, (e.pos().y() - a.y()) // 40
        if self.board.move(r * 4 + c):
            play("click")
            if self.board.solved():
                play("startup")
            self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        a = self.area()
        p.fillRect(a, QColor("#1c0a2e"))
        solved = self.board.solved()
        for pos, piece in enumerate(self.board.tiles):
            if piece == 15 and not solved:
                continue
            sr, sc = divmod(piece, 4)
            dr, dc = divmod(pos, 4)
            p.drawPixmap(QRect(a.x() + dc * 40, a.y() + dr * 40, 39, 39), self.picture,
                         QRect(sc * 40, sr * 40, 39, 39))
        p.setPen(ORANGE_HI if solved else DIM)
        p.setFont(font(8, True))
        p.drawText(QRect(0, a.bottom() + 4, self.width(), 18), Qt.AlignCenter,
                   "Solved! Click here to play again" if solved else "Click a tile next to the gap")
        p.end()


# ------------------------------------------------------------------ slide show
def find_pictures():
    pics = []
    for d in (os.path.join(HOME, "Pictures"),):
        for ext in ("jpg", "jpeg", "png", "JPG", "JPEG", "PNG"):
            pics += glob.glob(os.path.join(d, "*." + ext)) + glob.glob(os.path.join(d, "*", "*." + ext))
    if not pics:
        pics = glob.glob("/usr/share/wallpapers/*/contents/images/1920x1080.*")
    return sorted(set(pics))[:300]


class SlideShowGadget(Gadget):
    key, title, height_hint = "slides", "Slide Show", 150

    def __init__(self, host):
        super().__init__(host)
        self.pics = find_pictures()
        self.i = -1
        self.current = QPixmap()
        self.next_pic()
        t = QTimer(self)
        t.timeout.connect(self.next_pic)
        t.start(15000)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("Shows pictures from your Pictures folder. Click to open the picture.")

    def next_pic(self):
        if not self.pics:
            self.pics = find_pictures()
        if not self.pics:
            return
        self.i = (self.i + 1) % len(self.pics)
        img = QImage(self.pics[self.i])
        if not img.isNull():
            self.current = QPixmap.fromImage(img.scaled(W - 16, 120, Qt.KeepAspectRatioByExpanding,
                                                        Qt.SmoothTransformation))
        self.update()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton and self.pics:
            if shutil.which("xdg-open"):
                subprocess.Popen(["xdg-open", self.pics[self.i]], start_new_session=True,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self.paint_card(p, self.title)
        frame = QRect(8, 24, self.width() - 16, self.height() - 32)
        p.fillRect(frame, QColor("#1c0a2e"))
        if not self.current.isNull():
            src = QRect((self.current.width() - frame.width()) // 2, (self.current.height() - frame.height()) // 2,
                        frame.width(), frame.height())
            p.drawPixmap(frame, self.current, src)
        else:
            p.setPen(DIM)
            p.drawText(frame, Qt.AlignCenter | Qt.TextWordWrap, "Put pictures in your\nPictures folder")
        p.end()


GADGETS = [ClockGadget, CalendarGadget, CpuGadget, DiskGadget, NotesGadget, WeatherGadget,
           PuzzleGadget, SlideShowGadget]


# ------------------------------------------------------------------ the sidebar window
def column_layout(heights, avail_h):
    """Stack gadgets top to bottom; start a new column to the left when one is full.
    Returns [(column, y)] per gadget and the number of columns."""
    out, col, y = [], 0, 0
    for h in heights:
        if y > 0 and y + h > avail_h:
            col, y = col + 1, 0
        out.append((col, y))
        y += h + GAP
    return out, (col + 1 if heights else 1)


class Sidebar(QWidget):
    def __init__(self):
        super().__init__(None, Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnBottomHint)
        self.setWindowTitle("Gadgets")
        self.setAttribute(Qt.WA_X11NetWmWindowTypeDock, False)
        self.conf = dict(DEFAULTS)
        self.conf.update(load_json(CONF, {}))
        self.gadgets = {}
        self.build()
        scr = QApplication.primaryScreen()
        scr.availableGeometryChanged.connect(lambda _g: self.relayout())

    def save_conf(self):
        save_json(CONF, self.conf)

    def build(self):
        for g in self.gadgets.values():
            g.deleteLater()
        self.gadgets = {}
        for cls in GADGETS:
            if cls.key in self.conf["enabled"]:
                g = cls(self)
                g.resized.connect(self.relayout)
                g.show()
                self.gadgets[cls.key] = g
        self.relayout()

    def relayout(self):
        area = QApplication.primaryScreen().availableGeometry()
        order = [c.key for c in GADGETS if c.key in self.gadgets]
        heights = [self.gadgets[k].height() for k in order]
        pos, cols = column_layout(heights, area.height() - 2 * EDGE)
        width = cols * W + (cols - 1) * GAP
        self.setGeometry(area.right() - EDGE - width + 1, area.top() + EDGE, width, area.height() - 2 * EDGE)
        for k, (col, y) in zip(order, pos):
            self.gadgets[k].move(width - (col + 1) * W - col * GAP, y)
        self.update_mask()

    def update_mask(self):
        mask = QBitmap(self.size())
        mask.fill(Qt.color0)
        p = QPainter(mask)
        p.setBrush(Qt.color1)
        p.setPen(Qt.color1)
        for g in self.gadgets.values():
            p.drawRoundedRect(QRectF(g.geometry()).adjusted(0, 0, -1, -1), RADIUS, RADIUS)
        p.end()
        self.setMask(mask)

    def show_menu(self, gadget, pos):
        menu = QMenu(self)
        if gadget is not None:
            gadget.menu_actions(menu)
            if not menu.isEmpty():
                menu.addSeparator()
        gallery = menu.addMenu("Gadgets")
        for cls in GADGETS:
            a = QAction(cls.title, gallery)
            a.setCheckable(True)
            a.setChecked(cls.key in self.conf["enabled"])
            a.toggled.connect(lambda on, k=cls.key: self.toggle(k, on))
            gallery.addAction(a)
        menu.addSeparator()
        menu.addAction("Close gadgets", self.close_for_good)
        menu.exec_(pos)

    def contextMenuEvent(self, e):
        self.show_menu(None, e.globalPos())

    def toggle(self, key, on):
        enabled = [k for k in self.conf["enabled"] if k != key]
        if on:
            enabled.append(key)
        self.conf["enabled"] = [c.key for c in GADGETS if c.key in enabled]
        self.save_conf()
        self.build()

    def close_for_good(self):
        r = QMessageBox.question(None, "Gadgets",
                                 "Close the gadgets?\n\nThey won't start automatically any more. "
                                 "Open \"Gadgets\" from the start menu to bring them back.")
        if r == QMessageBox.Yes:
            self.conf["autostart"] = False
            self.save_conf()
            QApplication.quit()

    def notify(self, title, text):
        try:
            from PyQt5.QtDBus import QDBusInterface, QDBusConnection
            iface = QDBusInterface("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                   "org.freedesktop.Notifications", QDBusConnection.sessionBus())
            if iface.isValid():
                iface.call("Notify", "Gadgets", 0, "mikeos-amber", title, text, [], {}, 0)
                return
        except Exception:
            pass
        box = QMessageBox(QMessageBox.Information, title, text)
        box.setWindowFlag(Qt.WindowStaysOnTopHint, True)
        box.exec_()


def main():
    autostart = "--autostart" in sys.argv
    conf = dict(DEFAULTS)
    conf.update(load_json(CONF, {}))
    if autostart and not conf.get("autostart", True):
        return
    if not autostart and not conf.get("autostart", True):
        conf["autostart"] = True               # opened from the menu: start with every login again
        save_json(CONF, conf)
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return                                  # already running
    app = QApplication(sys.argv)
    app.setApplicationName("Gadgets")
    app.setDesktopFileName("mikeos-amber-gadgets")
    app.setQuitOnLastWindowClosed(False)
    bar = Sidebar()
    if QApplication.platformName() == "xcb":
        x11_window_states(bar.winId(), ["_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER",
                                        "_NET_WM_STATE_BELOW", "_NET_WM_STATE_STICKY"])
    bar.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
GADGETS_PY_EOF
chmod 755 "$APP"
cat > /usr/share/applications/mikeos-amber-gadgets.desktop <<'DESKTOP_EOF'
[Desktop Entry]
Type=Application
Name=Gadgets
Comment=Clock, calendar, sticky notes, weather and more on your desktop
Exec=python3 /usr/share/mikeos-amber/apps/gadgets.py
Icon=mikeos-amber
Categories=Utility;
DESKTOP_EOF
cat > /etc/xdg/autostart/mikeos-amber-gadgets.desktop <<'DESKTOP_EOF'
[Desktop Entry]
Type=Application
Name=Gadgets
Exec=python3 /usr/share/mikeos-amber/apps/gadgets.py --autostart
Icon=mikeos-amber
OnlyShowIn=KDE;LXQt;
NoDisplay=true
X-KDE-autostart-after=panel
DESKTOP_EOF
if ! python3 -c "import PyQt5.QtWidgets" 2>/dev/null; then
    echo " * Installing PyQt5 (needs internet)..."
    apt-get install -y python3-pyqt5 || { echo "Could not install python3-pyqt5. Connect to the internet and run this again."; exit 1; }
fi
echo " * Installed. They'll start by themselves every time you log in."

# start them now for whoever is logged into the desktop
USER_NAME=${SUDO_USER:-amber}
pid=$(pgrep -u "$USER_NAME" -x plasmashell | head -n 1)
if [ -n "$pid" ]; then
    envs=()
    for v in DISPLAY XAUTHORITY DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR WAYLAND_DISPLAY; do
        val=$(tr '\0' '\n' < "/proc/$pid/environ" | grep "^$v=" | head -n 1)
        [ -n "$val" ] && envs+=("$val")
    done
    sudo -u "$USER_NAME" env "${envs[@]}" setsid python3 "$APP" >/dev/null 2>&1 < /dev/null &
    echo " * Starting the gadgets now (they appear on the right side of the desktop)."
else
    echo " * Open \"Gadgets\" from the start menu, or log out and back in, to see them."
fi
echo "=== Done ==="
