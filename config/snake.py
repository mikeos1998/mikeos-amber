#!/usr/bin/env python3
"""Snake - Mike OS: Amber edition. Arrow keys or WASD to steer, Space to start, P to pause."""
import os
import random
import shutil
import subprocess
import sys

from PyQt5.QtCore import Qt, QTimer, QRect
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPen
from PyQt5.QtWidgets import QApplication, QWidget

COLS, ROWS, CELL = 24, 18, 24
HUD = 44
START_SPEED, MIN_SPEED, SPEEDUP = 130, 55, 3

BG = QColor("#1c0a2e")
GRID = QColor("#241038")
BORDER = QColor("#7a12c4")
HEAD = QColor("#ffb629")
BODY = QColor("#f08a00")
BODY_EDGE = QColor("#b44a00")
FOOD = QColor("#1e9e3a")
FOOD_SHINE = QColor("#7fe08f")
TEXT = QColor("#fff8e6")
ACCENT = QColor("#ffb629")

SOUNDS = "/usr/share/sounds/mikeos-amber"
SAVE_DIR = os.path.join(os.path.expanduser("~"), ".local", "share", "mikeos-amber")
SAVE_FILE = os.path.join(SAVE_DIR, "snake-highscore")

KEYS = {
    Qt.Key_Up: (0, -1), Qt.Key_W: (0, -1),
    Qt.Key_Down: (0, 1), Qt.Key_S: (0, 1),
    Qt.Key_Left: (-1, 0), Qt.Key_A: (-1, 0),
    Qt.Key_Right: (1, 0), Qt.Key_D: (1, 0),
}


def play(name):
    player = shutil.which("pw-play") or shutil.which("paplay")
    path = os.path.join(SOUNDS, name + ".ogg")
    if player and os.path.exists(path):
        try:
            subprocess.Popen([player, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass


def load_high():
    try:
        with open(SAVE_FILE) as f:
            return int(f.read().strip() or 0)
    except (OSError, ValueError):
        return 0


def save_high(value):
    try:
        os.makedirs(SAVE_DIR, exist_ok=True)
        with open(SAVE_FILE, "w") as f:
            f.write(str(value))
    except OSError:
        pass


class Game:
    """Board logic, kept separate from drawing."""

    def __init__(self, rng=None):
        self.rng = rng or random.Random()
        self.reset()

    def reset(self):
        cx, cy = COLS // 2, ROWS // 2
        self.snake = [(cx, cy), (cx - 1, cy), (cx - 2, cy)]
        self.direction = (1, 0)
        self.queue = []
        self.score = 0
        self.alive = True
        self.place_food()

    def place_food(self):
        free = [(x, y) for x in range(COLS) for y in range(ROWS) if (x, y) not in self.snake]
        self.food = self.rng.choice(free) if free else None

    def turn(self, d):
        last = self.queue[-1] if self.queue else self.direction
        if d == last or (d[0] == -last[0] and d[1] == -last[1]):
            return
        if len(self.queue) < 2:
            self.queue.append(d)

    def step(self):
        """Advance one tick. Returns 'eat', 'die' or None."""
        if not self.alive:
            return None
        if self.queue:
            self.direction = self.queue.pop(0)
        hx, hy = self.snake[0]
        nx, ny = hx + self.direction[0], hy + self.direction[1]
        grows = (nx, ny) == self.food
        body = self.snake if grows else self.snake[:-1]
        if not (0 <= nx < COLS and 0 <= ny < ROWS) or (nx, ny) in body:
            self.alive = False
            return "die"
        self.snake.insert(0, (nx, ny))
        if grows:
            self.score += 10
            self.place_food()
            return "eat"
        self.snake.pop()
        return None


class SnakeWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Snake")
        icon = QIcon.fromTheme("amber-snake")
        if not icon.isNull():
            self.setWindowIcon(icon)
        self.setFixedSize(COLS * CELL, ROWS * CELL + HUD)
        self.game = Game()
        self.high = load_high()
        self.state = "title"            # title, playing, paused, over
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.tick)
        self.font_big = QFont("DejaVu Sans Mono", 26, QFont.Bold)
        self.font_mid = QFont("DejaVu Sans Mono", 14, QFont.Bold)
        self.font_small = QFont("DejaVu Sans Mono", 11)

    # ---------------------------------------------------------------- flow
    def start(self):
        self.game.reset()
        self.state = "playing"
        self.timer.start(START_SPEED)
        play("question")
        self.update()

    def tick(self):
        result = self.game.step()
        if result == "eat":
            play("click")
            self.timer.setInterval(max(MIN_SPEED, START_SPEED - SPEEDUP * (self.game.score // 10)))
        elif result == "die":
            self.timer.stop()
            self.state = "over"
            play("error")
            if self.game.score > self.high:
                self.high = self.game.score
                save_high(self.high)
        self.update()

    def keyPressEvent(self, e):
        k = e.key()
        if k == Qt.Key_Escape:
            self.close()
        elif self.state in ("title", "over") and k in (Qt.Key_Space, Qt.Key_Return, Qt.Key_Enter):
            self.start()
        elif self.state == "playing" and k in KEYS:
            self.game.turn(KEYS[k])
        elif k == Qt.Key_P and self.state in ("playing", "paused"):
            if self.state == "playing":
                self.state = "paused"
                self.timer.stop()
            else:
                self.state = "playing"
                self.timer.start()
            self.update()

    def focusOutEvent(self, e):
        if self.state == "playing":
            self.state = "paused"
            self.timer.stop()
            self.update()
        super().focusOutEvent(e)

    # ---------------------------------------------------------------- drawing
    def cell(self, x, y, inset=0):
        return QRect(x * CELL + inset, HUD + y * CELL + inset, CELL - 2 * inset, CELL - 2 * inset)

    def paintEvent(self, _):
        p = QPainter(self)
        w, h = self.width(), self.height()
        p.fillRect(0, 0, w, h, BG)

        # HUD
        p.fillRect(0, 0, w, HUD - 4, QColor("#2c1247"))
        p.fillRect(0, HUD - 4, w, 4, BORDER)
        p.setFont(self.font_mid)
        p.setPen(ACCENT)
        p.drawText(QRect(14, 0, w // 2, HUD - 4), Qt.AlignVCenter | Qt.AlignLeft, "SCORE %05d" % self.game.score)
        p.setPen(TEXT)
        p.drawText(QRect(w // 2, 0, w // 2 - 14, HUD - 4), Qt.AlignVCenter | Qt.AlignRight, "HI %05d" % self.high)

        # grid dots
        p.setPen(QPen(GRID, 2))
        for x in range(1, COLS):
            for y in range(1, ROWS):
                p.drawPoint(x * CELL, HUD + y * CELL)

        # food
        if self.game.food:
            fx, fy = self.game.food
            p.fillRect(self.cell(fx, fy, 4), FOOD)
            p.fillRect(QRect(fx * CELL + 7, HUD + fy * CELL + 7, 5, 5), FOOD_SHINE)

        # snake
        if self.state != "title":
            for i, (x, y) in enumerate(reversed(self.game.snake)):
                head = i == len(self.game.snake) - 1
                p.fillRect(self.cell(x, y, 1), BODY_EDGE)
                p.fillRect(self.cell(x, y, 3), HEAD if head else BODY)
            hx, hy = self.game.snake[0]
            dx, dy = self.game.direction
            p.setPen(Qt.NoPen)
            for ex, ey in ((-dy, dx), (dy, -dx)):          # two eyes, facing forward
                cx = hx * CELL + CELL // 2 + ex * 5 + dx * 3
                cy = HUD + hy * CELL + CELL // 2 + ey * 5 + dy * 3
                p.fillRect(QRect(cx - 2, cy - 2, 4, 4), BG)

        # overlays
        if self.state == "title":
            self.banner(p, "SNAKE", "Press SPACE to play",
                        "Arrows / WASD to steer   P = pause   Esc = quit")
        elif self.state == "paused":
            self.banner(p, "PAUSED", "Press P to keep going")
        elif self.state == "over":
            best = "NEW HIGH SCORE!" if self.game.score and self.game.score == self.high else "Score %d" % self.game.score
            self.banner(p, "GAME OVER", best, "Press SPACE to play again")
        p.end()

    def banner(self, p, title, line1, line2=""):
        w = self.width()
        box = QRect(w // 2 - 230, HUD + 120, 460, 170)
        p.fillRect(box.adjusted(6, 6, 6, 6), QColor(0, 0, 0, 140))
        p.fillRect(box, QColor("#3a1f5c"))
        p.setPen(QPen(ACCENT, 3))
        p.drawRect(box)
        p.setFont(self.font_big)
        p.setPen(ACCENT)
        p.drawText(QRect(box.x(), box.y() + 18, box.width(), 50), Qt.AlignCenter, title)
        p.setFont(self.font_mid)
        p.setPen(TEXT)
        p.drawText(QRect(box.x(), box.y() + 78, box.width(), 30), Qt.AlignCenter, line1)
        if line2:
            p.setFont(self.font_small)
            p.drawText(QRect(box.x(), box.y() + 118, box.width(), 30), Qt.AlignCenter, line2)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Snake")
    app.setDesktopFileName("amber-snake")
    win = SnakeWindow()
    win.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
