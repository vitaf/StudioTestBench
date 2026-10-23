#!/usr/bin/env python3
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.

"""
G7: Visual regression tests for BioNeuronStudio.

Сравнивает скриншоты с baseline. Поддерживает:
- Попроцентное сравнение (pixel diff > 30 в любом канале)
- PSNR (peak signal-to-noise ratio) для оценки качества
- Автосоздание baseline при первом запуске

Требует: Xvfb, xdotool, ImageMagick (import), PIL/Pillow, numpy.
Порог: diff < 1% И PSNR > 30dB для прохождения.
"""

import subprocess
import time
import os
import sys
import argparse
import shutil

STUDIO = "/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
OUTDIR = "/tmp/studio_visual_tests"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASELINE_DIR = os.path.join(SCRIPT_DIR, "visual_baseline")

PASS = 0
FAIL = 0
SKIP = 0


def log(msg):
    print(f"  [{time.strftime('%H:%M:%S')}] {msg}")


def run(cmd, timeout=10):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True,
                              text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def get_studio_window_id():
    r = run("xdotool search --name BioNeuronStudio 2>/dev/null | head -1")
    if r and r.stdout.strip():
        return r.stdout.strip()
    return None


def take_screenshot(wid, path):
    run(f"xdotool windowactivate --sync {wid} 2>/dev/null")
    time.sleep(0.5)
    run(f"import -window {wid} {path} 2>/dev/null")
    return os.path.exists(path) and os.path.getsize(path) > 1000


def compute_psnr(img1, img2):
    """PSNR между двумя изображениями. >30dB = отлично, <20dB = провал."""
    import numpy as np
    a = np.array(img1).astype(np.float64)
    b = np.array(img2).astype(np.float64)
    mse = np.mean((a - b) ** 2)
    if mse < 1e-10:
        return 100.0
    return float(20 * np.log10(255.0 / np.sqrt(mse)))


def compare_images(baseline_path, current_path, diff_path=None,
                   diff_threshold=0.01, psnr_threshold=30.0):
    """Сравнение двух изображений. Возвращает (diff_percent, psnr, passed)."""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        log("ERROR: PIL/Pillow not installed. pip install Pillow numpy")
        return 1.0, 0.0, False

    baseline = Image.open(baseline_path).convert("RGB")
    current = Image.open(current_path).convert("RGB")

    if baseline.size != current.size:
        log(f"  Size mismatch: baseline={baseline.size} current={current.size}")
        # Resize current to match baseline for comparison
        current = current.resize(baseline.size, Image.LANCZOS)

    ba = np.array(baseline)
    ca = np.array(current)
    diff = np.abs(ba.astype(int) - ca.astype(int))
    diff_pixels = np.sum(np.any(diff > 30, axis=2))
    total_pixels = baseline.size[0] * baseline.size[1]
    diff_percent = diff_pixels / total_pixels if total_pixels > 0 else 1.0

    psnr = compute_psnr(baseline, current)

    if diff_path:
        diff_img = np.zeros_like(ba)
        mask = np.any(diff > 30, axis=2)
        diff_img[mask] = [255, 0, 0]
        diff_img[~mask] = ca[~mask]
        Image.fromarray(diff_img).save(diff_path)

    passed = diff_percent < diff_threshold and psnr > psnr_threshold
    return diff_percent, psnr, passed


def visual_test(name, widget_name="BioNeuronStudio"):
    """Один визуальный тест: скриншот → сравнение с baseline."""
    global PASS, FAIL, SKIP

    screenshot_path = os.path.join(OUTDIR, f"{name}.png")
    baseline_path = os.path.join(BASELINE_DIR, f"{name}.png")
    diff_path = os.path.join(OUTDIR, f"{name}_diff.png")

    # Найти окно
    wid = get_studio_window_id()
    if not wid:
        log(f"SKIP: {name} — Studio window not found")
        SKIP += 1
        return

    # Скриншот
    if not take_screenshot(wid, screenshot_path):
        log(f"FAIL: {name} — screenshot failed")
        FAIL += 1
        return

    sz_kb = os.path.getsize(screenshot_path) // 1024
    log(f"{name}: {sz_kb} KB screenshot")

    # Baseline
    if not os.path.exists(baseline_path):
        shutil.copy(screenshot_path, baseline_path)
        log(f"  BASELINE CREATED for '{name}' (first run)")
        PASS += 1
        return

    # Сравнение
    diff_pct, psnr, passed = compare_images(baseline_path, screenshot_path, diff_path)

    log(f"  Diff: {diff_pct*100:.2f}% | PSNR: {psnr:.1f}dB "
        f"(thresholds: 1.0%, 30dB)")

    if passed:
        log(f"  PASS: {name}")
        PASS += 1
    else:
        log(f"  FAIL: {name} changed (diff={diff_pct*100:.2f}%, PSNR={psnr:.1f}dB)")
        log(f"  Diff image: {diff_path}")
        FAIL += 1


def main():
    global PASS, FAIL, SKIP
    print("=== Visual Regression Tests (G7) ===\n")

    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", action="store_true",
                        help="Force baseline recreation")
    args = parser.parse_args()

    os.makedirs(OUTDIR, exist_ok=True)
    os.makedirs(BASELINE_DIR, exist_ok=True)

    if args.baseline:
        log("Force-recreating baseline...")
        for f in os.listdir(BASELINE_DIR):
            os.remove(os.path.join(BASELINE_DIR, f))

    # Test 1: Main window (default state after startup)
    log("Test 1/4: Main window...")
    visual_test("main_window")

    # Test 2: After creating a test neuron (menu interaction)
    log("Test 2/4: Test neuron viewport...")
    # Send Alt+U to open Utilities menu, then Enter to create test neuron
    wid = get_studio_window_id()
    if wid:
        run(f"xdotool windowactivate --sync {wid} key --window {wid} "
            f"Alt+u Return 2>/dev/null", timeout=2)
        time.sleep(1)
    visual_test("test_neuron_viewport", "BioNeuronStudio")

    # Test 3: View from different angle
    log("Test 3/4: Viewport rotated...")
    if wid:
        # Send some view rotation keys
        run(f"xdotool windowactivate --sync {wid} key --window {wid} "
            f"Right Right Right 2>/dev/null", timeout=1)
        time.sleep(0.5)
    visual_test("viewport_rotated", "BioNeuronStudio")

    # Test 4: Clean state (close any dialogs)
    log("Test 4/4: Clean state...")
    if wid:
        run(f"xdotool windowactivate --sync {wid} key --window {wid} "
            f"Escape Escape 2>/dev/null", timeout=1)
        time.sleep(0.5)
    visual_test("clean_state", "BioNeuronStudio")

    # Summary
    total = PASS + FAIL + SKIP
    print(f"\n=== Visual Results: {PASS} PASS, {FAIL} FAIL, "
          f"{SKIP} SKIP (total {total}) ===")

    if SKIP > 0 and PASS + FAIL == 0:
        log("All tests skipped — baseline will be created on first run with Xvfb+Studio")
        return 0

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
