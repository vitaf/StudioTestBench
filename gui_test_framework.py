#!/usr/bin/env python3
"""GUI test framework for BioNeuronStudio. Headless automation via xdotool."""
import subprocess, time, os, sys, shlex

STUDIO = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
OUTDIR = "/tmp/studio_gui_tests"

class GuiTest:
    def __init__(self):
        self.proc = None
        self.wid = None
        self.results = []

    def run(self, cmd, timeout=5):
        return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)

    def log(self, msg):
        print(f"  [{time.strftime('%H:%M:%S')}] {msg}")

    def start_studio(self):
        self.log("Starting Studio...")
        os.makedirs(OUTDIR, exist_ok=True)
        self.proc = subprocess.Popen([STUDIO], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Wait for window
        for _ in range(40):
            time.sleep(0.5)
            r = self.run("xdotool search --name BioNeuronStudio 2>/dev/null | head -1")
            if r.stdout.strip():
                self.wid = r.stdout.strip()
                break
        if not self.wid:
            self.log("FAIL: window not found")
            return False
        self.log(f"Window: {self.wid}")
        time.sleep(0.5)
        return True

    def focus(self):
        self.run(f"xdotool windowactivate --sync {self.wid} 2>/dev/null")
        time.sleep(0.2)

    def key(self, keys):
        """Send keystroke to Studio window."""
        self.focus()
        self.run(f"xdotool key --window {self.wid} {keys} 2>/dev/null")
        time.sleep(0.3)

    def type_text(self, text):
        self.focus()
        self.run(f"xdotool type --window {self.wid} --delay 5 {shlex.quote(text)} 2>/dev/null")

    def click(self, x, y):
        """Click at window-relative coordinates."""
        self.focus()
        self.run(f"xdotool mousemove --window {self.wid} {x} {y} 2>/dev/null")
        time.sleep(0.1)
        self.run(f"xdotool click --window {self.wid} 1 2>/dev/null")
        time.sleep(0.3)

    def screenshot(self, name):
        path = f"{OUTDIR}/{name}.png"
        self.run(f"import -window {self.wid} {path} 2>/dev/null")
        size = os.path.getsize(path) if os.path.exists(path) else 0
        self.log(f"  screenshot {name}: {size//1024} KB")
        return path if size > 1000 else None

    def verify(self, name, condition, msg):
        if condition:
            self.results.append(("PASS", name, msg))
        else:
            self.results.append(("FAIL", name, msg))
        status = "PASS" if condition else "FAIL"
        self.log(f"  [{status}] {name}: {msg}")

    def stop(self):
        if self.proc:
            self.proc.kill()
            self.proc.wait()
            self.proc = None
            time.sleep(0.5)

    def report(self):
        p = sum(1 for r in self.results if r[0] == "PASS")
        f = sum(1 for r in self.results if r[0] == "FAIL")
        print(f"\n=== GUI TEST REPORT: {p} PASS, {f} FAIL ===")
        for r in self.results:
            print(f"  [{r[0]}] {r[1]}: {r[2]}")

def analyze_screenshot(path):
    """Analyze screenshot with PIL: detect viewport content."""
    try:
        from PIL import Image
        img = Image.open(path)
        w, h = img.size
        # Analyze center-left region (viewport area)
        vp = img.crop((w//8, h//8, w//2, h*7//8))
        pixels = list(vp.getdata())
        n = len(pixels)
        dark = sum(1 for p in pixels if max(p) < 40)
        mid = sum(1 for p in pixels if 40 <= max(p) < 120)
        bright = sum(1 for p in pixels if max(p) >= 120)
        avg = (sum(p[0] for p in pixels)//n, sum(p[1] for p in pixels)//n, sum(p[2] for p in pixels)//n)
        return {"dark_pct": dark*100//n, "mid_pct": mid*100//n, "bright_pct": bright*100//n, "avg_rgb": avg}
    except ImportError:
        return None
