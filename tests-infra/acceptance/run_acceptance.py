#!/usr/bin/env python3
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
# Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

"""
run_acceptance.py — приёмочные тесты MindCopy.

Запускает ключевые сценарии через скомпилированные C++ тесты и проверяет
соответствие ожидаемому поведению. Аналогичен BDD, но без behave.
Для Gherkin-версии: behave features/ (требуется pip install behave).
"""

import subprocess
import os
import sys
import re

MINDCOPY_ROOT = os.environ.get("MINDCOPY_ROOT",
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
BUILD_DIR = os.path.join(MINDCOPY_ROOT, "BioNeuronStudio", "build")
PASS = 0
FAIL = 0


def log(msg):
    print(f"  [{msg}]")


def run_exe(name):
    path = os.path.join(BUILD_DIR, "tests", name)
    if not os.path.exists(path):
        return -1, f"NOT FOUND: {path}"
    try:
        r = subprocess.run([path], cwd=BUILD_DIR, capture_output=True, text=True, timeout=120)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return -1, f"TIMEOUT: {name}"


def scenario(title, exe, checks):
    """Запускает exe и применяет список проверок (callable -> bool)."""
    global PASS, FAIL
    print(f"\n  Scenario: {title}")
    rc, out = run_exe(exe)
    all_ok = True
    for desc, fn in checks:
        ok = fn(out, rc)
        status = "PASS" if ok else "FAIL"
        print(f"    [{status}] {desc}")
        if not ok:
            all_ok = False
    if all_ok:
        PASS += 1
    else:
        FAIL += 1


# ── Проверки ────────────────────────────────────────────────────────

def check_no_orphans(out, rc):
    # Ищем "Всего сирот ...: 0 /" или "сирот=0"
    return re.search(r'Всего сирот.*:\s*0\s/', out) is not None or \
           "сирот=0" in out or \
           "0 orphans" in out.lower()

def check_neuron_growth(out, rc):
    return "neurons decreased" not in out

def check_cpg_active(out, rc):
    m = re.search(r'active_frames=(\d+)', out)
    return m and int(m.group(1)) >= 3

def check_din_found(out, rc):
    return "No left dIN" not in out and "No right dIN" not in out

def check_roundtrip_ok(out, rc):
    return "neuron count mismatch" not in out.lower() and \
           "segment count mismatch" not in out.lower() and \
           rc == 0

def check_bion_rejected(out, rc):
    return "Should reject" not in out  # тест ожидает false, значит OK

# ═══════════════════════════════════════════════════════════════════

def main():
    global PASS, FAIL

    print("=== Acceptance Tests (BDD-equivalent) ===\n")

    # Сценарий 1: Все 4 стадии генерируются без сирот
    scenario(
        "Все 4 стадии генерируются без сирот",
        "sim_tadpole_stages",
        [
            ("Все стадии содержат нейроны и сегменты", lambda o, rc: rc == 0 and "FAILED" not in o),
            ("Нет сегментов-сирот", check_no_orphans),
            ("Количество нейронов растёт", check_neuron_growth),
        ]
    )

    # Сценарий 2: FREE_SWIMMING показывает CPG-активность
    scenario(
        "FREE_SWIMMING показывает CPG-активность",
        "sim_tadpole_stages",
        [
            ("Спайковая активность >= 3/10 кадров", check_cpg_active),
            ("dIN нейроны найдены и стимулированы", check_din_found),
        ]
    )

    # Сценарий 3: Сеть сохраняется и загружается без потерь
    scenario(
        "Сеть сохраняется и загружается без потерь (roundtrip)",
        "sim_security",
        [
            ("Количество нейронов совпадает", lambda o, rc: "neuron count mismatch" not in o.lower()),
            ("Количество сегментов совпадает", lambda o, rc: "segment count mismatch" not in o.lower()),
            ("Координаты первого сегмента идентичны", lambda o, rc: rc == 0),
        ]
    )

    # Сценарий 4: Отказоустойчивость симуляции
    scenario(
        "Симуляция устойчива к некорректным операциям",
        "sim_chaos",
        [
            ("Double start не крашит", lambda o, rc: rc == 0),
            ("Stop без start не крашит", lambda o, rc: rc == 0),
            ("Start-Stop-Start работает", lambda o, rc: rc == 0),
        ]
    )

    # ── Новые сценарии: Core, Orchestrator, Agent ──

    CORE_BUILD  = os.path.join(MINDCOPY_ROOT, "BioNeuronCore", "build")
    ORCH_BUILD  = os.path.join(MINDCOPY_ROOT, "BioNeuronOrchestrator", "build")

    def run_core_exe(name):
        path = os.path.join(CORE_BUILD, name)
        if not os.path.exists(path):
            return -1, f"NOT FOUND: {path}"
        try:
            r = subprocess.run([path], cwd=CORE_BUILD, capture_output=True, text=True, errors='replace', timeout=120)
            return r.returncode, r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            return -1, f"TIMEOUT: {name}"
        except OSError as e:
            return -1, f"OS ERROR: {e}"

    def run_orch_exe(name):
        path = os.path.join(ORCH_BUILD, name)
        if not os.path.exists(path):
            return -1, f"NOT FOUND: {path}"
        try:
            r = subprocess.run([path], cwd=ORCH_BUILD, capture_output=True, text=True, errors='replace', timeout=120)
            return r.returncode, r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            return -1, f"TIMEOUT: {name}"
        except OSError as e:
            return -1, f"OS ERROR: {e}"

    def scenario_core(title, exe, checks):
        global PASS, FAIL
        print(f"\n  Scenario: {title}")
        rc, out = run_core_exe(exe)
        all_ok = True
        for desc, fn in checks:
            ok = fn(out, rc)
            print(f"    [{'PASS' if ok else 'FAIL'}] {desc}")
            if not ok: all_ok = False
        if all_ok: PASS += 1
        else: FAIL += 1

    def scenario_orch(title, exe, checks):
        global PASS, FAIL
        print(f"\n  Scenario: {title}")
        rc, out = run_orch_exe(exe)
        all_ok = True
        for desc, fn in checks:
            ok = fn(out, rc)
            print(f"    [{'PASS' if ok else 'FAIL'}] {desc}")
            if not ok: all_ok = False
        if all_ok: PASS += 1
        else: FAIL += 1

    # Сценарий 5: Демон загружает плагины и сеть
    scenario_core(
        "Демон загружает builtin-плагины и принимает соединения",
        "test_core_smoke",
        [("Демон smoke-тест пройден", lambda o, rc: rc == 0)]
    )

    # Сценарий 6: Плагины Core загружаются корректно
    scenario_core(
        "Все builtin-плагины загружаются и имеют имена",
        "test_plugin_lifecycle",
        [("Плагины загружены, имена валидны", lambda o, rc: rc == 0)]
    )

    # Сценарий 7: BION-файлы с битыми заголовками отвергаются
    scenario_core(
        "Битые BION-файлы отвергаются без краша",
        "test_core_security",
        [("Security тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 8: Регрессии не вернулись
    scenario_core(
        "Константы и размеры структур стабильны (regression)",
        "test_core_regression",
        [("Regression тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 9: Core производительность
    scenario_core(
        "Производительность Core в допустимых пределах",
        "test_core_performance",
        [("Performance тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 10: Оркестратор — регистрация узла и API
    scenario_orch(
        "Оркестратор обрабатывает API запросы",
        "test_orch_api",
        [("API тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 11: Оркестратор — безопасность
    scenario_orch(
        "Оркестратор отклоняет неавторизованные запросы",
        "test_orch_security",
        [("Security тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 12: Агент — сборка аргументов демона
    scenario_orch(
        "Агент корректно формирует аргументы демона",
        "test_agent_executor",
        [("Executor тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 13: Агент — интеграция
    scenario_orch(
        "Агент парсит JSON и выполняет задачи",
        "test_agent_integration",
        [("Integration тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 14: Криптография оркестратора
    scenario_orch(
        "Криптография: AES-256-GCM + SHA-256 + PBKDF2",
        "test_orch_crypto",
        [("Crypto тесты пройдены", lambda o, rc: rc == 0)]
    )

    # Сценарий 15: Очередь оркестратора
    scenario_orch(
        "Очередь задач работает с MockDatabase",
        "test_orchestrator_queue",
        [("Queue тесты пройдены", lambda o, rc: rc == 0)]
    )

    print(f"\n=== Acceptance Results: {PASS} PASS, {FAIL} FAIL ===")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
