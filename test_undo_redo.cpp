// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_undo_redo.cpp — тесты Undo/Redo алгоритма.
// Проверяет: push+undo, redo, лимит 50, дедупликация по кадру,
// очистка redo-стека при новом push, работа с пустыми стеками.

#include <gtest/gtest.h>
#include <vector>
#include <utility>

// ═══════════════════════════════════════════════════════════════════
// Standalone UndoStack — повторяет логику Application без GUI.
//
// Семантика как в Application::pushUndo():
//   pushUndo(model) — сохраняет ТЕКУЩЕЕ состояние перед модификацией.
//   После pushUndo пользователь меняет model, затем следующий pushUndo
//   сохраняет уже новое состояние, и т.д.
//   performUndo(model) — восстанавливает предыдущее сохранённое состояние.
//   performRedo(model) — возвращает отменённое состояние.
//
// Примечание: рендер-цикл инкрементирует current_frame_ до любого
// пользовательского действия, поэтому первый push всегда на кадре >= 1.
// ═══════════════════════════════════════════════════════════════════

template <typename T>
struct UndoStack {
    std::vector<T> undo_stack_;
    std::vector<T> redo_stack_;
    static constexpr int kMaxUndoDepth = 50;
    int last_undo_frame_ = 0;
    int current_frame_ = 0;

    void pushUndo(const T& model) {
        if (current_frame_ == last_undo_frame_) return;
        last_undo_frame_ = current_frame_;
        undo_stack_.push_back(model);
        if ((int)undo_stack_.size() > kMaxUndoDepth)
            undo_stack_.erase(undo_stack_.begin());
        redo_stack_.clear();
    }

    bool performUndo(T& model) {
        if (undo_stack_.empty()) return false;
        redo_stack_.push_back(std::move(model));
        model = undo_stack_.back();
        undo_stack_.pop_back();
        last_undo_frame_ = 0;
        return true;
    }

    bool performRedo(T& model) {
        if (redo_stack_.empty()) return false;
        undo_stack_.push_back(std::move(model));
        model = redo_stack_.back();
        redo_stack_.pop_back();
        last_undo_frame_ = 0;
        return true;
    }

    void nextFrame() { current_frame_++; }
};

// ═══════════════════════════════════════════════════════════════════
// Тесты с int-значениями
// ═══════════════════════════════════════════════════════════════════

TEST(UndoStack, PushUndoRestoresPreviousState) {
    UndoStack<int> us;

    // Модель: сначала S0=10
    int model = 10;

    // Действие A: сохраняем S0, меняем на S1
    us.nextFrame();
    us.pushUndo(model);  // сохранили 10
    model = 20;

    // Действие B: сохраняем S1, меняем на S2
    us.nextFrame();
    us.pushUndo(model);  // сохранили 20
    model = 30;

    // Undo: S2 → S1
    ASSERT_TRUE(us.performUndo(model));
    EXPECT_EQ(model, 20);

    // Undo: S1 → S0
    ASSERT_TRUE(us.performUndo(model));
    EXPECT_EQ(model, 10);
}

TEST(UndoStack, RedoReturnsUndoneState) {
    UndoStack<int> us;

    int model = 100;
    us.nextFrame(); us.pushUndo(model); model = 200;
    us.nextFrame(); us.pushUndo(model); model = 300;

    // Undo 300→200, затем 200→100
    ASSERT_TRUE(us.performUndo(model)); EXPECT_EQ(model, 200);
    ASSERT_TRUE(us.performUndo(model)); EXPECT_EQ(model, 100);

    // Redo: 100→200, 200→300
    ASSERT_TRUE(us.performRedo(model)); EXPECT_EQ(model, 200);
    ASSERT_TRUE(us.performRedo(model)); EXPECT_EQ(model, 300);
}

TEST(UndoStack, Limit50OldestEvicted) {
    UndoStack<int> us;
    int model = 0;
    for (int i = 0; i < 60; i++) {
        us.nextFrame();
        us.pushUndo(model);  // сохраняем текущее
        model = i + 1;       // переходим к следующему
    }

    EXPECT_EQ((int)us.undo_stack_.size(), UndoStack<int>::kMaxUndoDepth);

    // Самые старые (10 штук) вытеснены.
    // Стек: [10, 11, 12, ..., 59] = последние 50 сохранённых состояний.
    EXPECT_EQ(us.undo_stack_.front(), 10);
    EXPECT_EQ(us.undo_stack_.back(), 59);
}

TEST(UndoStack, DedupByFrameOneSnapshotPerFrame) {
    UndoStack<int> us;

    int model = 10;
    us.nextFrame();
    us.pushUndo(model);  // сохранили 10
    us.pushUndo(11);     // тот же кадр — игнорируется
    us.pushUndo(12);     // тот же кадр — игнорируется

    EXPECT_EQ((int)us.undo_stack_.size(), 1);
    EXPECT_EQ(us.undo_stack_[0], 10);

    // Переходим на следующий кадр, меняем модель, сохраняем
    us.nextFrame();
    model = 20;
    us.pushUndo(model);  // сохранили 20
    us.pushUndo(21);     // тот же кадр — игнорируется
    EXPECT_EQ((int)us.undo_stack_.size(), 2);

    // Модифицируем модель без сохранения (симуляция действия пользователя)
    model = 30;
    // Undo: 30→20 (последнее сохранённое)
    ASSERT_TRUE(us.performUndo(model));
    EXPECT_EQ(model, 20);
}

TEST(UndoStack, NewPushClearsRedoStack) {
    UndoStack<int> us;

    int model = 1;
    us.nextFrame(); us.pushUndo(model); model = 2;
    us.nextFrame(); us.pushUndo(model); model = 3;

    // Undo 3→2, 2→1
    us.performUndo(model);  // model=2, redo=[3]
    us.performUndo(model);  // model=1, redo=[3,2]
    EXPECT_EQ((int)us.redo_stack_.size(), 2);

    // Новое изменение очищает redo
    us.nextFrame();
    us.pushUndo(model);  // сохранили 1
    model = 99;           // новое состояние
    EXPECT_EQ((int)us.redo_stack_.size(), 0);
}

TEST(UndoStack, EmptyUndoDoesNothing) {
    UndoStack<int> us;
    int model = 42;
    ASSERT_FALSE(us.performUndo(model));
    EXPECT_EQ(model, 42);
}

TEST(UndoStack, EmptyRedoDoesNothing) {
    UndoStack<int> us;

    int model = 5;
    us.nextFrame(); us.pushUndo(model); model = 10;

    us.performUndo(model);  // 10→5, redo=[10]
    ASSERT_TRUE(us.performRedo(model));  // 5→10
    // Redo стек пуст
    ASSERT_FALSE(us.performRedo(model));
    EXPECT_EQ(model, 10);
}

TEST(UndoStack, MultipleUndoRedoCycles) {
    UndoStack<int> us;

    int model = 1;
    us.nextFrame(); us.pushUndo(model); model = 2;
    us.nextFrame(); us.pushUndo(model); model = 3;

    // Цикл: undo → redo → undo → redo
    us.performUndo(model); EXPECT_EQ(model, 2);  // 3→2
    us.performRedo(model); EXPECT_EQ(model, 3);  // 2→3
    us.performUndo(model); EXPECT_EQ(model, 2);  // 3→2
    us.performUndo(model); EXPECT_EQ(model, 1);  // 2→1
    us.performRedo(model); EXPECT_EQ(model, 2);  // 1→2
    us.performRedo(model); EXPECT_EQ(model, 3);  // 2→3
    ASSERT_FALSE(us.performRedo(model));
}

TEST(UndoStack, UndoAfterNewPushClearsRedoAndKeepsModel) {
    UndoStack<int> us;

    int model = 1;
    us.nextFrame(); us.pushUndo(model); model = 2;
    us.nextFrame(); us.pushUndo(model); model = 3;

    // Undo 2 раза: 3→2, 2→1. redo=[2,3]
    us.performUndo(model);  // model=2
    us.performUndo(model);  // model=1
    EXPECT_EQ((int)us.redo_stack_.size(), 2);

    // Новое изменение очищает redo
    us.nextFrame();
    us.pushUndo(model);  // сохранили 1
    model = 100;          // новое состояние
    EXPECT_EQ((int)us.redo_stack_.size(), 0);

    // Undo возвращает к 1
    us.performUndo(model);
    EXPECT_EQ(model, 1);

    // Redo возвращает 100
    us.performRedo(model);
    EXPECT_EQ(model, 100);
}

TEST(UndoStack, FrameCounterWrapsCorrectly) {
    UndoStack<int> us;
    us.current_frame_ = 2147483000;
    us.last_undo_frame_ = 2147483000;
    us.pushUndo(42);  // dedup — игнорируется

    EXPECT_EQ((int)us.undo_stack_.size(), 0);

    us.nextFrame();  // 2147483001
    us.pushUndo(42);
    EXPECT_EQ((int)us.undo_stack_.size(), 1);
}

// ═══════════════════════════════════════════════════════════════════
// Тесты с NetworkModel (Studio data_model)
// ═══════════════════════════════════════════════════════════════════

#include "../BioNeuronStudio/src/data_model.h"

TEST(UndoStack, NetworkModelPushUndoRestoresSegments) {
    UndoStack<NetworkModel> us;

    NetworkModel model;
    SegmentSnapshot seg1 = {};
    seg1.id = 1;
    seg1.position = {0, 0, 0};
    seg1.direction = {0, 1, 0};
    seg1.length = 10.0f;
    seg1.radius = 1.0f;
    seg1.V_m = -65.0f;
    model.segments.push_back(seg1);
    model.next_id = 2;

    us.nextFrame();
    us.pushUndo(model);  // сохранили 1 сегмент

    // Добавляем второй сегмент
    SegmentSnapshot seg2 = {};
    seg2.id = 2;
    seg2.position = {10, 0, 0};
    seg2.direction = {0, 1, 0};
    seg2.length = 5.0f;
    seg2.radius = 0.5f;
    seg2.V_m = -70.0f;
    model.segments.push_back(seg2);
    model.next_id = 3;

    // Undo → состояние с 1 сегментом
    ASSERT_TRUE(us.performUndo(model));
    EXPECT_EQ(model.segments.size(), 1u);
    EXPECT_EQ(model.segments[0].id, 1u);
    EXPECT_FLOAT_EQ(model.segments[0].V_m, -65.0f);
    EXPECT_EQ(model.next_id, 2u);

    // Redo → 2 сегмента снова
    ASSERT_TRUE(us.performRedo(model));
    EXPECT_EQ(model.segments.size(), 2u);
    EXPECT_EQ(model.next_id, 3u);
}

TEST(UndoStack, NetworkModelLimit50) {
    UndoStack<NetworkModel> us;
    NetworkModel current;
    for (int i = 0; i < 60; i++) {
        SegmentSnapshot seg = {};
        seg.id = (uint32_t)(i + 1);
        seg.V_m = -50.0f - i;
        current.segments.push_back(seg);
        current.next_id = seg.id + 1;
        us.nextFrame();
        us.pushUndo(current);  // сохраняем текущее (i сегментов)
    }

    EXPECT_EQ((int)us.undo_stack_.size(), 50);
    // Сохранены состояния с 11-го по 60-й (последние 50)
    EXPECT_EQ(us.undo_stack_.front().segments.size(), 11u);
    EXPECT_EQ(us.undo_stack_.back().segments.size(), 60u);
}

TEST(UndoStack, NetworkModelDedupByFrame) {
    UndoStack<NetworkModel> us;

    NetworkModel model;
    SegmentSnapshot seg = {};
    seg.id = 1;
    seg.V_m = -60.0f;
    model.segments.push_back(seg);
    model.next_id = 2;

    us.nextFrame();
    us.pushUndo(model);  // сохранили V_m=-60

    seg.V_m = -50.0f;
    model.segments[0] = seg;
    us.pushUndo(model);  // тот же кадр — игнорируется

    EXPECT_EQ((int)us.undo_stack_.size(), 1);
    EXPECT_FLOAT_EQ(us.undo_stack_[0].segments[0].V_m, -60.0f);
}
