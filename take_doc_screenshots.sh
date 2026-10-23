#!/bin/bash
# Copyright (c) 2026 Фешин Виталий Александрович. Все права защищены.
# Скриншоты для регистрации. JPEG 300 DPI, 1920x1080.
# Версия 2: исправлена навигация по меню (точные индексы).

set -e
STUDIO="/home/vitaf/workarea/develop/MindCopy/BioNeuronStudio/build/BioNeuronStudio"
OUT="/home/vitaf/workarea/develop/MindCopy/doc/screenshots"
rm -f "$OUT"/*.jpg
mkdir -p "$OUT"

take_shot() { echo "  [$1] $2"; import -window root -density 300 -quality 95 "$OUT/${1}.jpg"; }
send_keys() { for k in "$@"; do xdotool key "$k"; sleep 0.08; done; }
menu_down() { for i in $(seq 1 $1); do xdotool key Down; sleep 0.06; done; }
submenu_right() { xdotool key Right; sleep 0.4; }
close_menu() { xdotool key Escape; sleep 0.2; }

echo "=== Doc Screenshots v2 ==="

# ── Xvfb ──
pkill -f "Xvfb :99" 2>/dev/null || true; sleep 1
rm -f /tmp/.X99-lock /tmp/.X11-unix/X99
Xvfb :99 -screen 0 1920x1080x24 -dpi 300 -ac +extension RANDR &
XVFB_PID=$!; sleep 2; export DISPLAY=:99

# ── Studio ──
$STUDIO & STUDIO_PID=$!
for i in $(seq 1 60); do
    WID=$(xdotool search --name BioNeuronStudio 2>/dev/null | head -1)
    [ -n "$WID" ] && break; sleep 1
done
[ -z "$WID" ] && { kill $STUDIO_PID $XVFB_PID 2>/dev/null; exit 1; }
sleep 4

# ── 1. Main window ──
take_shot "01_main_window" "Главное окно"

# ── 2. Load single neuron ──
xdotool key ctrl+o; sleep 1
xdotool type "/home/vitaf/workarea/develop/MindCopy/examples/test_single_neuron.bion"
xdotool key Return; sleep 5
take_shot "02_single_neuron" "Тестовый нейрон (v6)"

# ── 3. Cell Library ──
xdotool key alt+l; sleep 0.5  # Лаборатория
menu_down 6  # Канюли, Графики, Эксперимент, Запись видео, Эксперименты, Библиотека клеток
xdotool key Return; sleep 2
take_shot "03_cell_library" "Библиотека клеток"

# ── 4. File → Import submenu ──
close_menu
xdotool key alt+f; sleep 0.5  # Файл
menu_down 8  # Новый..Выход, в т.ч. сепараторы (8 to reach Импорт)
submenu_right
sleep 0.5
take_shot "04_import_menu" "Файл→Импорт (SWC+NML)"

# ── 5. File → Export submenu ──
xdotool key Down; sleep 0.3  # from Import to Export
submenu_right
sleep 0.5
take_shot "05_export_menu" "Файл→Экспорт (SWC+NML)"

# ── 6. Utilities ──
close_menu
xdotool key alt+w; sleep 0.5  # Окно
menu_down 10  # Иерархия..Утилиты
xdotool key Return; sleep 2
take_shot "06_converter" "Утилиты (конвертер)"

# ── 7. Tadpole network ──
close_menu
xdotool key alt+l; sleep 0.5  # Лаборатория
menu_down 7  # ... Подопытные (7-й элемент)
submenu_right
xdotool key Return  # Головастик (1-й элемент)
sleep 2
# Enable body checkbox (Tab to first checkbox)
xdotool key Tab; sleep 0.2; xdotool key space; sleep 0.2  # 3D-тело ON
xdotool key Tab; sleep 0.2; xdotool key space; sleep 0.2  # Среда ON
# Generate button (Tab to Generate)
xdotool key Tab; sleep 0.2; xdotool key Tab; sleep 0.2
xdotool key Tab; sleep 0.2; xdotool key Tab; sleep 0.2
xdotool key Return; sleep 10  # Click Generate
take_shot "07_tadpole_panel" "Головастик: панель с сетью CPG"

# ── 8. 3D viewport ──
sleep 2
take_shot "08_tadpole_3d" "3D-вьюпорт с головастиком"

# ── Cleanup ──
kill $STUDIO_PID 2>/dev/null; wait $STUDIO_PID 2>/dev/null || true
kill $XVFB_PID 2>/dev/null; wait $XVFB_PID 2>/dev/null || true
echo "Done: $(ls "$OUT"/*.jpg | wc -l) screenshots"
