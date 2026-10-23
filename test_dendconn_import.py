#!/usr/bin/env python3
"""Сценарный тест BioNeuronStudio: импорт DendConn → скриншоты."""
import subprocess, time, os, sys, shlex

STUDIO_BIN = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
NML_FILE  = "/home/vitaf/workarea/develop/MindCopy/examples/neuroml/networks/DendConn.net.nml"
OUT_DIR   = "/tmp/studio_test"

def run(cmd, timeout=5):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)

def log(msg):
    print(f"  [{time.strftime('%H:%M:%S')}] {msg}")

def find_window():
    for _ in range(30):
        r = run("xdotool search --name BioNeuronStudio 2>/dev/null | head -1")
        if r.stdout.strip():
            return r.stdout.strip()
        time.sleep(0.5)
    return None

def screenshot(name, wid=None):
    if not wid:
        wid = find_window()
    if not wid:
        log(f"SKIP {name}: окно не найдено")
        return False
    run(f"xdotool windowactivate {wid}")
    time.sleep(0.3)
    path = f"{OUT_DIR}/{name}.png"
    run(f"import -window {wid} {path}")
    size = os.path.getsize(path) if os.path.exists(path) else 0
    log(f"Скриншот {name}: {size//1024} KB")
    return size > 1000

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # 1. Запуск
    log("Запуск BioNeuronStudio...")
    proc = subprocess.Popen([STUDIO_BIN], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(5)
    wid = find_window()
    if not wid:
        log("ОШИБКА: окно не появилось")
        proc.kill(); return 1
    log(f"Окно: {wid}")

    try:
        # 2. Активируем окно
        run(f"xdotool windowactivate {wid}"); time.sleep(0.3)

        # 3. Ctrl+O → открыть файл
        log("Ctrl+O → открыть файл...")
        run("xdotool key ctrl+o"); time.sleep(0.8)
        run(f"xdotool type --delay 3 {shlex.quote(NML_FILE)}"); time.sleep(0.3)
        run("xdotool key Return"); time.sleep(3)

        screenshot("01_after_import", wid)

        # 4. Запуск симуляции (Ctrl+R)
        log("Симуляция Ctrl+R...")
        run("xdotool key ctrl+r"); time.sleep(3)

        screenshot("02_simulation_3s", wid)

        # 5. Стоп (Ctrl+T для паузы или повторно для стопа)
        run("xdotool key ctrl+t"); time.sleep(0.5)

        screenshot("03_stopped", wid)

        log("ГОТОВО. Скриншоты: " + OUT_DIR)

    finally:
        proc.kill()
        proc.wait()

if __name__ == "__main__":
    sys.exit(main())
