// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_viewport_recorder.cpp — тесты ViewportRecorder:
//   - потоковая запись (кадры сразу на диск)
//   - переворот строк (OpenGL bottom-up → top-down)
//   - MJPEG/Uncompressed форматы
//   - пауза/возобновление

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <sys/stat.h>

#include "../BioNeuronStudio/src/viewport_recorder.h"

static const char* TEST_AVI = "/tmp/test_recorder_streaming.avi";
static const int W = 64, H = 48;

// Генерирует тестовый RGB-кадр: градиент, где верхняя строка = красный, нижняя = синий
static std::vector<uint8_t> make_test_frame(int w, int h) {
    std::vector<uint8_t> data(w * h * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 3;
            data[idx + 0] = (uint8_t)(x * 255 / w);           // R: градиент по X
            data[idx + 1] = (uint8_t)(y * 255 / h);           // G: градиент по Y
            data[idx + 2] = (uint8_t)((x + y) * 127 / (w+h)); // B: смесь
        }
    }
    return data;
}

// Проверяет, что верхняя строка изображения красная (R>>B), а не синяя.
// glReadPixels даёт bottom-up (нижняя строка первой). После переворота
// верхняя строка должна быть с максимальным Y (т.е. зелёный канал высокий).
static bool check_top_row_is_green(const std::vector<uint8_t>& data, int w) {
    // Верхняя строка: y=h-1 → зелёный канал ~255
    for (int x = 0; x < w; x += 8) {
        int idx = x * 3;  // первая строка после flip = бывшая последняя
        if (data[idx + 1] < 200) return false;  // G должен быть высоким
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Потоковая запись: кадры пишутся на диск сразу, не буферизуются в памяти
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ViewportRecorder, Streaming_FileCreatedOnStart) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));
    // Файл должен существовать сразу после StartRecording (заголовок записан)
    struct stat st;
    ASSERT_EQ(stat(TEST_AVI, &st), 0);
    ASSERT_GT(st.st_size, 100);  // заголовок AVI > 100 байт
    rec.StopRecording();
    remove(TEST_AVI);
}

TEST(ViewportRecorder, Streaming_FileGrowsWithFrames) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));

    auto frame = make_test_frame(W, H);
    struct stat st;
    stat(TEST_AVI, &st);
    off_t size_before = st.st_size;
    ASSERT_GT(size_before, 100) << "Заголовок должен быть записан при StartRecording";

    // Пишем 301 кадр с микро-паузой (1ms) — как в реальном rendering loop
    float sim_time = 0.0f;
    for (int i = 0; i < 301; i++) {
        rec.CaptureFrame(frame.data(), W, H, sim_time);
        sim_time += 1.0f / 30.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    rec.WaitForQueueEmpty();

    stat(TEST_AVI, &st);
    ASSERT_GT(st.st_size, size_before * 2) << "Файл должен вырасти после 300+ кадров (flush)";

    rec.StopRecording();
    remove(TEST_AVI);
}

TEST(ViewportRecorder, Streaming_FramesNotBufferedInMemory) {
    // Косвенный тест: записываем много кадров — не должно быть OOM
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));

    auto frame = make_test_frame(W, H);
    float sim_time = 0.0f;
    for (int i = 0; i < 500; i++) {
        rec.CaptureFrame(frame.data(), W, H, sim_time);
        sim_time += 0.034f;  // чуть > 1/30 чтобы избежать float-погрешности
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    rec.WaitForQueueEmpty();
    // 500 кадров × ~2KB JPEG = ~1MB на диске, ~0 в памяти (только смещения)
    EXPECT_EQ(rec.FrameCount(), 500);

    bool ok = rec.StopRecording();
    ASSERT_TRUE(ok);

    struct stat st;
    stat(TEST_AVI, &st);
    ASSERT_GT(st.st_size, 100000);  // минимум 100KB для 500 JPEG-кадров
    remove(TEST_AVI);
}

TEST(ViewportRecorder, Streaming_FpsThrottle) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 10.0f, RecordFormat::AVI_MJPEG));  // 10 fps

    auto frame = make_test_frame(W, H);
    // 3 кадра в пределах 1/10 секунды → только первый должен записаться
    rec.CaptureFrame(frame.data(), W, H, 0.0f);
    rec.CaptureFrame(frame.data(), W, H, 0.05f);  // слишком рано
    rec.CaptureFrame(frame.data(), W, H, 0.09f);  // слишком рано
    EXPECT_EQ(rec.FrameCount(), 1);

    // Следующий через 0.1s — записывается
    rec.CaptureFrame(frame.data(), W, H, 0.11f);
    EXPECT_EQ(rec.FrameCount(), 2);

    rec.StopRecording();
    remove(TEST_AVI);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Переворот строк (OpenGL bottom-up → top-down)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ViewportRecorder, RowFlip_BottomUpToTopDown) {
    // Создаём фрейм где верхняя строка красная (R=255), нижняя синяя (B=255)
    // OpenGL даст это как bottom-up → после flip верх должен быть красным
    std::vector<uint8_t> gl_data(W * H * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 3;
            // В OpenGL-порядке: y=0 = низ изображения
            if (y == 0) {
                gl_data[idx+0]=0; gl_data[idx+1]=0; gl_data[idx+2]=255;  // низ=синий
            } else if (y == H-1) {
                gl_data[idx+0]=255; gl_data[idx+1]=0; gl_data[idx+2]=0;  // верх=красный
            } else {
                gl_data[idx+0]=(uint8_t)(y*255/H); gl_data[idx+1]=(uint8_t)(y*255/H); gl_data[idx+2]=(uint8_t)(y*255/H);
            }
        }
    }

    // Проверяем: верхняя строка (y=H-1) должна быть красной в исходных данных
    EXPECT_EQ(gl_data[((H-1)*W + 0)*3 + 0], 255);  // R=255
    EXPECT_EQ(gl_data[((H-1)*W + 0)*3 + 2], 0);    // B=0

    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_UNCOMPRESSED));
    rec.CaptureFrame(gl_data.data(), W, H, 0.0f);
    rec.StopRecording();

    // Читаем AVI и проверяем pixel data
    FILE* f = fopen(TEST_AVI, "rb");
    ASSERT_TRUE(f);
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> avi_data(fsize);
    fread(avi_data.data(), 1, fsize, f);
    fclose(f);

    // Ищем 'movi' и первый кадр
    bool found = false;
    for (size_t i = 0; i < fsize - 4; i++) {
        if (memcmp(avi_data.data() + i, "movi", 4) == 0) {
            // После movi идёт 00db + size + данные
            size_t pos = i + 4;  // после "movi"
            if (pos + 8 < fsize && memcmp(avi_data.data() + pos, "00db", 4) == 0) {
                uint32_t frame_size = avi_data[pos+4] | (avi_data[pos+5]<<8) | (avi_data[pos+6]<<16) | (avi_data[pos+7]<<24);
                pos += 8;
                // Первая строка данных (после flip = бывшая H-1 = красная)
                if (pos + 3 < fsize) {
                    EXPECT_EQ(avi_data[pos + 0], 255) << "Flip: верхняя строка должна быть красной (R=255)";
                    EXPECT_EQ(avi_data[pos + 2], 0)   << "Flip: верхняя строка должна быть красной (B=0)";
                    found = true;
                }
                break;
            }
        }
    }
    ASSERT_TRUE(found) << "Не найден movi/00db в AVI";
    remove(TEST_AVI);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Форматы и жизненный цикл
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ViewportRecorder, PauseResume) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));
    auto frame = make_test_frame(W, H);

    rec.CaptureFrame(frame.data(), W, H, 0.0f);
    EXPECT_EQ(rec.FrameCount(), 1);

    rec.Pause();
    ASSERT_TRUE(rec.IsPaused());
    rec.CaptureFrame(frame.data(), W, H, 0.1f);  // на паузе — не пишем
    EXPECT_EQ(rec.FrameCount(), 1);

    rec.Resume();
    ASSERT_FALSE(rec.IsPaused());
    rec.CaptureFrame(frame.data(), W, H, 0.1f);
    EXPECT_EQ(rec.FrameCount(), 2);

    rec.StopRecording();
    remove(TEST_AVI);
}

TEST(ViewportRecorder, StopRecording_ReturnsFalseWhenNoFrames) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));
    // Не пишем кадры
    bool ok = rec.StopRecording();
    EXPECT_FALSE(ok);
    remove(TEST_AVI);
}

TEST(ViewportRecorder, DoubleInit_ReturnsFalse) {
    remove(TEST_AVI);
    ViewportRecorder rec;
    ASSERT_TRUE(rec.StartRecording(TEST_AVI, W, H, 30.0f, RecordFormat::AVI_MJPEG));
    EXPECT_FALSE(rec.StartRecording("/tmp/other.avi", W, H, 30.0f, RecordFormat::AVI_MJPEG));
    rec.StopRecording();
    remove(TEST_AVI);
}

TEST(ViewportRecorder, EdgeCases_ZeroSize) {
    ViewportRecorder rec;
    EXPECT_FALSE(rec.StartRecording(TEST_AVI, 0, 0, 30.0f, RecordFormat::AVI_MJPEG));
    EXPECT_FALSE(rec.StartRecording(TEST_AVI, 0, 720, 30.0f, RecordFormat::AVI_MJPEG));
    EXPECT_FALSE(rec.StartRecording(TEST_AVI, 1280, 0, 30.0f, RecordFormat::AVI_MJPEG));
}
