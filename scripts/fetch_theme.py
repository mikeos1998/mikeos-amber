#!/usr/bin/env python3
"""Fetch the Reactionary theme (GPLv3, by phob1an), recolor it purple,
and install it into the image as the "Amber" Plasma theme + window decoration."""
import colorsys, gzip, json, os, re, shutil, subprocess, sys, tarfile, zipfile

REPO = os.environ.get("REACTIONARY_REPO", "https://www.opencode.net/phob1an/reactionary.git")
REF = os.environ.get("REACTIONARY_REF", "")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INC = os.path.join(ROOT, "config/includes.chroot")
WORK = os.path.join(ROOT, ".cache/reactionary")
PURPLE_HUE = 278 / 360.0


def die(msg, tree=None):
    print("\nERROR: " + msg, file=sys.stderr)
    if tree:
        print("Files found in the Reactionary download:", file=sys.stderr)
        for base, dirs, files in os.walk(tree):
            dirs[:] = [d for d in dirs if d != ".git"]
            for f in sorted(files):
                print("  " + os.path.relpath(os.path.join(base, f), tree), file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------- recolor
def shift(r, g, b):
    """Blue/teal shades become purple, greys become purple-tinted; everything else is kept."""
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    if max(r, g, b) - min(r, g, b) <= 30:                 # a grey (panel, menu, borders)
        if not 0.12 <= l <= 0.93:
            return None                                   # keep black text and white highlights
        r, g, b = colorsys.hls_to_rgb(PURPLE_HUE, l - 0.03, 0.35 + 0.3 * l)
    elif 0.5 <= h <= 0.78 and s >= 0.25 and 0.05 <= l <= 0.95:
        if l < 0.5:
            l = 0.36 + 0.28 * l
        r, g, b = colorsys.hls_to_rgb(PURPLE_HUE, l, min(1.0, s * 1.1))
    else:
        return None
    return round(r * 255), round(g * 255), round(b * 255)


HEX = re.compile(r"(?<![\w(&])#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-zA-Z_-])")
RGB = re.compile(r"rgb\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\)")


def recolor_text(text):
    count = 0

    def hexsub(m):
        nonlocal count
        if text[max(0, m.start() - 6):m.start()].rstrip("\"'").endswith("href="):
            return m.group(0)                      # a link to an element id, not a color
        v = m.group(1)
        if len(v) == 3:
            v = "".join(c * 2 for c in v)
        new = shift(int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16))
        if not new:
            return m.group(0)
        count += 1
        return "#%02x%02x%02x" % new

    def rgbsub(m):
        nonlocal count
        new = shift(*(min(255, int(x)) for x in m.groups()))
        if not new:
            return m.group(0)
        count += 1
        return "rgb(%d,%d,%d)" % new

    return RGB.sub(rgbsub, HEX.sub(hexsub, text)), count


def recolor_tree(top):
    total = 0
    for base, dirs, files in os.walk(top):
        dirs[:] = [d for d in dirs if d != "icons"]       # leave the small tray icons alone
        for name in files:
            path = os.path.join(base, name)
            if name.endswith(".svgz"):
                with gzip.open(path, "rb") as f:
                    text = f.read().decode("utf-8", "replace")
                text, n = recolor_text(text)
                if n:
                    with gzip.open(path, "wb") as f:
                        f.write(text.encode("utf-8"))
            elif name.endswith(".svg"):
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                text, n = recolor_text(text)
                if n:
                    with open(path, "w", encoding="utf-8") as f:
                        f.write(text)
            else:
                continue
            total += n
    return total


# ---------------------------------------------------------------- fetch
def fetch():
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(os.path.dirname(WORK), exist_ok=True)
    subprocess.run(["git", "clone"] + ([] if REF else ["--depth", "1"]) + [REPO, WORK], check=True)
    if REF:
        subprocess.run(["git", "-C", WORK, "checkout", REF], check=True)
    commit = subprocess.run(["git", "-C", WORK, "rev-parse", "HEAD"], check=True,
                            capture_output=True, text=True).stdout.strip()
    # the repo may hold packed themes; unpack them next to the rest
    n = 0
    for base, dirs, files in os.walk(WORK):
        dirs[:] = [d for d in dirs if d not in (".git", "_unpacked")]
        for name in files:
            path = os.path.join(base, name)
            out = os.path.join(WORK, "_unpacked", str(n))
            try:
                if tarfile.is_tarfile(path):
                    with tarfile.open(path) as t:
                        safe = [m for m in t.getmembers()
                                if (m.isfile() or m.isdir())
                                and not m.name.startswith("/") and ".." not in m.name.split("/")]
                        t.extractall(out, members=safe)
                elif name.endswith(".zip") and zipfile.is_zipfile(path):
                    with zipfile.ZipFile(path) as z:
                        z.extractall(out)
                else:
                    continue
                n += 1
            except (tarfile.TarError, zipfile.BadZipFile, OSError) as e:
                print("skipping archive %s: %s" % (name, e))
    return commit


def find(kind):
    hits = []
    for base, dirs, files in os.walk(WORK):
        dirs[:] = [d for d in dirs if d != ".git"]
        meta = "metadata.desktop" in files or "metadata.json" in files
        if kind == "aurorae" and ("decoration.svg" in files or "decoration.svgz" in files):
            hits.append(base)
        if kind == "plasma" and meta and "contents" not in dirs and ("widgets" in dirs or "colors" in files):
            hits.append(base)
    # prefer the plain "Reactionary" variant, then the shortest path
    hits.sort(key=lambda p: (os.path.basename(p).lower() != "reactionary", len(p), p))
    return hits


def rename_metadata(path):
    for name in ("metadata.desktop", "metadata.json"):
        f = os.path.join(path, name)
        if not os.path.exists(f):
            continue
        with open(f, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        if name.endswith(".desktop"):
            text = re.sub(r"(?m)^Name\[[^\]]*\]=.*\n", "", text)
            text = re.sub(r"(?m)^Name=.*$", "Name=Amber", text)
            text = re.sub(r"(?m)^X-KDE-PluginInfo-Name=.*$", "X-KDE-PluginInfo-Name=Amber", text)
        else:
            data = json.loads(text)
            plugin = data.setdefault("KPlugin", {})
            for key in [k for k in plugin if k.startswith("Name[")]:
                del plugin[key]
            plugin["Id"] = plugin["Name"] = "Amber"
            text = json.dumps(data, indent=4) + "\n"
        with open(f, "w", encoding="utf-8") as fh:
            fh.write(text)


def install(src, dest):
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(src, dest, symlinks=False, ignore=shutil.ignore_patterns(".git"))
    rename_metadata(dest)


def main():
    commit = fetch()
    plasma, aurorae = find("plasma"), find("aurorae")
    if not plasma:
        die("could not find the Reactionary Plasma theme in the download.", WORK)
    if not aurorae:
        die("could not find the Reactionary window decoration in the download.", WORK)

    p_dest = os.path.join(INC, "usr/share/plasma/desktoptheme/Amber")
    a_dest = os.path.join(INC, "usr/share/aurorae/themes/Amber")
    install(plasma[0], p_dest)
    install(aurorae[0], a_dest)

    # Aurorae looks for "<ThemeName>rc"
    for name in os.listdir(a_dest):
        if name.endswith("rc") and name != "Amberrc" and os.path.isfile(os.path.join(a_dest, name)):
            os.replace(os.path.join(a_dest, name), os.path.join(a_dest, "Amberrc"))
    # if the Plasma theme carries its own palette, make it the Amber one
    if os.path.exists(os.path.join(p_dest, "colors")):
        shutil.copyfile(os.path.join(INC, "usr/share/color-schemes/Amber.colors"), os.path.join(p_dest, "colors"))

    n_p, n_a = recolor_tree(p_dest), recolor_tree(a_dest)

    doc = os.path.join(INC, "usr/share/doc/mikeos-amber")
    os.makedirs(doc, exist_ok=True)
    with open(os.path.join(doc, "CREDITS"), "w") as f:
        f.write("The Mike OS: Amber Plasma theme and window decoration are a recolored fork of\n"
                "Reactionary by phob1an, licensed under the GNU GPL version 3.\n"
                "Source: %s\nCommit: %s\n" % (REPO, commit))
    for lic in ("LICENSE", "COPYING"):
        if os.path.exists(os.path.join(WORK, lic)):
            shutil.copyfile(os.path.join(WORK, lic), os.path.join(doc, "LICENSE.Reactionary"))
            break

    rel = lambda p: os.path.relpath(p, WORK)
    print("Reactionary commit   : " + commit)
    print("Plasma theme         : %s  (%d colors changed)" % (rel(plasma[0]), n_p))
    print("Window decoration    : %s  (%d colors changed)" % (rel(aurorae[0]), n_a))
    if len(plasma) > 1 or len(aurorae) > 1:
        print("Other variants found : " + ", ".join(rel(p) for p in plasma[1:] + aurorae[1:]))


if __name__ == "__main__":
    main()
