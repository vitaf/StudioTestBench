#!/usr/bin/env python3
"""Сценарий: импорт NeuroML DendConn → симуляция → анализ скриншотов."""
import subprocess, time, os, sys

STUDIO = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
NML    = "/home/vitaf/workarea/develop/MindCopy/examples/neuroml/networks/DendConn.net.nml"
OUT    = "/tmp/studio_test"

def cmd(c, t=5):
    return subprocess.run(c, shell=True, capture_output=True, text=True, timeout=t)

def log(msg):
    print(f"  [{time.strftime('%H:%M:%S')}] {msg}")

def find_win():
    for _ in range(40):
        r = cmd("xdotool search --name BioNeuronStudio 2>/dev/null | head -1")
        if r.stdout.strip(): return r.stdout.strip()
        time.sleep(0.5)

def shot(name, wid):
    cmd(f"xdotool windowactivate {wid}"); time.sleep(0.3)
    path = f"{OUT}/{name}.png"
    cmd(f"import -window {wid} {path}")
    sz = os.path.getsize(path) if os.path.exists(path) else 0
    log(f"  {name}: {sz//1024} KB")
    return sz > 1000

def analyze():
    """Анализ скриншотов через PIL."""
    try:
        from PIL import Image
    except ImportError:
        log("PIL not available, skip analysis")
        return

    log("=== Анализ скриншотов ===")
    for fn in sorted(os.listdir(OUT)):
        if not fn.endswith('.png'): continue
        img = Image.open(f"{OUT}/{fn}")
        w, h = img.size

        # Ищем viewport: тёмная область в центре
        # Сканируем центр изображения
        cx, cy = w//2, h//2
        # viewport — примерно левая половина центральной области
        vp = img.crop((50, 80, w//2, h-40))
        px = list(vp.getdata())
        n = len(px)

        # Статистика
        dark  = sum(1 for p in px if max(p) < 50)    # фон
        mid   = sum(1 for p in px if 50 <= max(p) < 150)  # сегменты/UI
        bright = sum(1 for p in px if max(p) >= 150)      # яркое
        red   = sum(1 for p in px if p[0] > p[1]+30 and p[0] > p[2]+30)
        green = sum(1 for p in px if p[1] > p[0]+30 and p[1] > p[2]+30)
        blue  = sum(1 for p in px if p[2] > p[0]+30 and p[2] > p[1]+30)

        avg = (sum(p[0] for p in px)//n, sum(p[1] for p in px)//n, sum(p[2] for p in px)//n)

        log(f"  {fn}: avg=rgb({avg[0]},{avg[1]},{avg[2]}) dark={dark*100//n}% "
            f"mid={mid*100//n}% bright={bright*100//n}% "
            f"R={red} G={green} B={blue}")

def main():
    os.makedirs(OUT, exist_ok=True)

    log("Запуск Studio...")
    proc = subprocess.Popen([STUDIO], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(5)
    wid = find_win()
    if not wid: log("ОШИБКА: окно не найдено"); proc.kill(); return 1
    log(f"Окно: {wid}")

    try:
        # Активируем
        cmd(f"xdotool windowactivate {wid}"); time.sleep(0.3)

        # Файл → Импорт NeuroML
        # Alt+F открывает меню Файл, затем 9 раз вниз до "Импорт NeuroML..."
        log("Меню: Файл → Импорт NeuroML...")
        cmd("xdotool key alt+f"); time.sleep(0.4)
        for _ in range(9):
            cmd("xdotool key Down"); time.sleep(0.03)
        cmd("xdotool key Return"); time.sleep(0.8)

        # zenity диалог: вводим путь
        cmd(f"xdotool type --delay 3 '{NML}'"); time.sleep(0.3)
        cmd("xdotool key Return"); time.sleep(4)  # ждём импорт + отрисовку

        # Включаем сглаживание стыков
        log("Вид → Сглаживать стыки")
        cmd("xdotool key alt+v"); time.sleep(0.3)
        for _ in range(8):
            cmd("xdotool key Down"); time.sleep(0.02)
        cmd("xdotool key Return"); time.sleep(0.3)

        shot("01_imported", wid)

        # Запуск симуляции
        log("Симуляция: Старт")
        cmd("xdotool key ctrl+r"); time.sleep(4)
        shot("02_simulating", wid)

        # Стоп
        cmd("xdotool key ctrl+t"); time.sleep(0.5)
        shot("03_stopped", wid)

        log("ГОТОВО")
        analyze()

    finally:
        proc.kill()
        proc.wait()

if __name__ == "__main__":
    sys.exit(main())
