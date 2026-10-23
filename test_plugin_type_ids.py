#!/usr/bin/env python3
# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
# Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

# test_plugin_type_ids.py — валидация согласованности type ID между .desc и .cpp файлами плагинов.
#
# Проверяет:
#   1. Для каждого .desc с парным .cpp: type_id в JSON должен совпадать с встроенным JSON в .cpp
#   2. Для каждого .cpp с парным .desc: type_id должен быть в допустимом диапазоне (0-MOD_TYPE_COUNT-1)
#   3. Нет дубликатов type_id среди разных модификаторов (current vs release одного типа — ок)
#   4. ID в dispatch_generated.cpp соответствуют .desc в SDK
#
# Возвращает 0 если всё чисто, 1 если есть проблемы.

import os
import sys
import json
import re
from pathlib import Path


def extract_ids_from_desc(path):
    """Извлекает все type_id из .desc JSON-файла. Возвращает список int."""
    try:
        with open(path) as f:
            data = json.load(f)
    except (json.JSONDecodeError, KeyError) as e:
        print(f"  ERROR parsing {path}: {e}")
        return []
    modifiers = data.get('modifiers', [data])
    if not isinstance(modifiers, list):
        modifiers = [modifiers]
    return [m['id'] for m in modifiers if 'id' in m]


def extract_json_ids_from_cpp(path):
    """Извлекает все \\\"id\\\":N из встроенного JSON в .cpp файле. Возвращает список int."""
    try:
        with open(path) as f:
            content = f.read()
    except IOError:
        return []
    # Ищем встроенные JSON-строки: "id":121 или \"id\":121
    ids = set()
    for m in re.finditer(r'"id"\s*:\s*(\d+)', content):
        ids.add(int(m.group(1)))
    return sorted(ids)


def extract_register_ids_from_cpp(path):
    """Извлекает type_id из register_modifier_current/modulator_release вызовов в .cpp."""
    try:
        with open(path) as f:
            content = f.read()
    except IOError:
        return []
    ids = set()
    for m in re.finditer(r'(?:BIOCORE_MOD_(?:CURRENT|RELEASE)|register_modifier_(?:current|modulator_release))\s*\(\s*(\d+)', content):
        ids.add(int(m.group(1)))
    return sorted(ids)


def extract_dispatch_ids(dispatch_path):
    """Извлекает type_id из case-ов в dispatch_generated.cpp."""
    try:
        with open(dispatch_path) as f:
            content = f.read()
    except IOError:
        return set()
    ids = set()
    for m in re.finditer(r'case\s+(\d+)\s*:', content):
        ids.add(int(m.group(1)))
    return ids


def read_mod_type_count(types_h_path):
    """Читает MOD_TYPE_COUNT из types.h. Возвращает int или None."""
    try:
        with open(types_h_path) as f:
            content = f.read()
    except IOError:
        return None
    m = re.search(r'#define\s+MOD_TYPE_COUNT\s+(\d+)', content)
    return int(m.group(1)) if m else None


def check_directory(plugins_dir, label, max_type_id):
    """Проверяет одну директорию плагинов. Возвращает (errors, warnings)."""
    plugins_dir = Path(plugins_dir)
    errors = []
    warnings = []

    if not plugins_dir.exists():
        warnings.append(f"Directory not found: {plugins_dir}")
        return errors, warnings

    desc_files = sorted(plugins_dir.glob("*.desc"))

    # Проверка 1: соответствие .desc ↔ .cpp
    for desc_path in desc_files:
        base = desc_path.stem
        cpp_path = plugins_dir / f"{base}.cpp"

        desc_ids = extract_ids_from_desc(desc_path)
        if not desc_ids:
            warnings.append(f"{desc_path.name}: no 'id' field in modifiers")
            continue

        for tid in desc_ids:
            if tid < 0 or tid > max_type_id:
                errors.append(f"{desc_path.name}: type_id={tid} outside valid range [0,{max_type_id}]")

        if cpp_path.exists():
            cpp_json_ids = extract_json_ids_from_cpp(cpp_path)
            cpp_reg_ids = extract_register_ids_from_cpp(cpp_path)

            # Сравниваем .desc ID с встроенным JSON в .cpp
            desc_set = set(desc_ids)
            cpp_set = set(cpp_json_ids) if cpp_json_ids else set(cpp_reg_ids)

            if cpp_set and desc_set != cpp_set:
                errors.append(
                    f"MISMATCH {base}: .desc={desc_ids}, .cpp={sorted(cpp_set)}"
                )
        # else: .desc без .cpp — ок (встроенные типы из Core)

    # Проверка 2: дубликаты type_id (с учётом что current+release одного типа — не дубликат)
    id_to_files = {}
    for desc_path in desc_files:
        desc_ids = extract_ids_from_desc(desc_path)
        for tid in desc_ids:
            if tid not in id_to_files:
                id_to_files[tid] = []
            id_to_files[tid].append(desc_path.name)

    for tid, files in sorted(id_to_files.items()):
        if len(files) > 1:
            # Различаем по имени: mod_NN_X.desc vs mod_NN.desc — это current+release одного типа
            # Постфикс после номера указывает на вариант (current vs release)
            stems = [Path(f).stem for f in files]
            # Если один файл = mod_NN, другой = mod_NN_something — это легитимный дубликат
            has_base = any(s == f"mod_{tid:02d}" for s in stems)
            has_variant = any(s != f"mod_{tid:02d}" and s.startswith(f"mod_{tid:02d}_") for s in stems)
            if has_base and has_variant:
                continue  # OK: current + release варианты
            errors.append(f"DUPLICATE type_id={tid}: {files}")

    return errors, warnings


def main():
    # Определяем пути
    script_dir = Path(__file__).resolve().parent
    mindcopy_root = script_dir.parent  # StudioTestBench, предполагаем что тест в нём

    # Пути для проверки
    core_builtin = mindcopy_root / "BioNeuronCore" / "plugins" / "builtin"
    sdk_examples = mindcopy_root / "BioNeuronSDK" / "examples"
    sdk_build = mindcopy_root / "BioNeuronSDK" / "build" / "plugins"
    dispatch_file = mindcopy_root / "BioNeuronCore" / "src" / "dispatch_generated.cpp"

    # Верхний допустимый type_id = MOD_TYPE_COUNT-1 (из types.h)
    types_h = mindcopy_root / "BioNeuronCore" / "include" / "types.h"
    mod_type_count = read_mod_type_count(types_h)
    max_type_id = (mod_type_count - 1) if mod_type_count else 196
    print(f"MOD_TYPE_COUNT={mod_type_count} → max valid type_id={max_type_id}\n")

    all_errors = []
    all_warnings = []

    # Проверяем Core builtin плагины
    print("=== Checking Core builtin plugins ===")
    errs, warns = check_directory(core_builtin, "Core builtin", max_type_id)
    all_errors.extend(errs)
    all_warnings.extend(warns)
    for w in warns:
        print(f"  WARNING: {w}")
    for e in errs:
        print(f"  ERROR: {e}")
    if not errs:
        print("  OK")

    # Проверяем SDK examples (source .desc)
    print("\n=== Checking SDK examples ===")
    errs, warns = check_directory(sdk_examples, "SDK examples", max_type_id)
    all_errors.extend(errs)
    all_warnings.extend(warns)
    for w in warns:
        print(f"  WARNING: {w}")
    for e in errs:
        print(f"  ERROR: {e}")
    if not errs:
        print("  OK")

    # Проверяем SDK build plugins (сравниваем с source)
    print("\n=== Checking SDK build/plugins vs examples ===")
    for example_desc in sorted(sdk_examples.glob("*.desc")):
        build_desc = sdk_build / example_desc.name
        if build_desc.exists():
            ex_ids = extract_ids_from_desc(example_desc)
            bd_ids = extract_ids_from_desc(build_desc)
            if ex_ids != bd_ids:
                all_errors.append(
                    f"SDK BUILD MISMATCH {example_desc.name}: "
                    f"examples={ex_ids}, build={bd_ids}"
                )
                print(f"  ERROR: {all_errors[-1]}")
        else:
            all_warnings.append(f"SDK build/{example_desc.name} not found (stale build?)")
            print(f"  WARNING: {all_warnings[-1]}")
    if not any("SDK BUILD MISMATCH" in e for e in all_errors):
        print("  OK")

    # Проверяем dispatch_generated.cpp ↔ SDK examples
    print(f"\n=== Checking dispatch_generated.cpp: {dispatch_file} ===")
    if dispatch_file.exists():
        dispatch_ids = extract_dispatch_ids(dispatch_file)
        sdk_ids = set()
        for desc in sdk_examples.glob("*.desc"):
            sdk_ids.update(extract_ids_from_desc(desc))
        user_sdk_ids = {i for i in sdk_ids if 121 <= i <= 127}

        if user_sdk_ids != dispatch_ids:
            all_errors.append(
                f"DISPATCH mismatch: dispatch_generated.cpp={dispatch_ids}, "
                f"SDK user types={user_sdk_ids}"
            )
            print(f"  ERROR: {all_errors[-1]}")
        else:
            print(f"  OK (types: {sorted(dispatch_ids)})")
    else:
        all_warnings.append(f"dispatch_generated.cpp not found at {dispatch_file}")
        print(f"  WARNING: {all_warnings[-1]}")

    # Итог
    print(f"\n{'='*60}")
    print(f"Errors: {len(all_errors)}, Warnings: {len(all_warnings)}")
    if all_errors:
        print("FAILED — type ID inconsistencies found!")
        return 1
    else:
        print("PASSED — all type IDs consistent")
        return 0


if __name__ == '__main__':
    sys.exit(main())
