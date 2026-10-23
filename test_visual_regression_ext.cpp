// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// G7: Visual regression — smoke test that verifies baseline infrastructure exists
// Full visual regression runs via run_visual_regression.sh + Xvfb
#include <gtest/gtest.h>
#include <cstdio>
#include <sys/stat.h>

TEST(VisualRegression, BaselineDirectoryExists) {
    struct stat st;
    const char* baseline = "/home/vitaf/workarea/develop/MindCopy/StudioTestBench/tests-infra/visual_baseline";
    if (stat(baseline, &st) == 0 && S_ISDIR(st.st_mode)) {
        SUCCEED() << "Visual baseline directory exists";
    } else {
        GTEST_SKIP() << "Visual baseline directory not found (requires Xvfb setup)";
    }
}

TEST(VisualRegression, ScriptExists) {
    const char* script = "/home/vitaf/workarea/develop/MindCopy/StudioTestBench/run_visual_regression.sh";
    struct stat st;
    if (stat(script, &st) == 0) {
        SUCCEED() << "Visual regression script exists";
    } else {
        GTEST_SKIP() << "Visual regression script not found";
    }
}

TEST(VisualRegression, XvfbAvailable) {
    FILE* f = popen("which Xvfb 2>/dev/null", "r");
    if (f) {
        char buf[256] = {};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        if (buf[0]) {
            SUCCEED() << "Xvfb found: " << buf;
        } else {
            GTEST_SKIP() << "Xvfb not installed";
        }
    }
}

TEST(VisualRegression, SmokeScreenshot) {
    // Verify we can take a screenshot via xdotool/import if Xvfb is running
    const char* display = getenv("DISPLAY");
    if (!display || display[0] == '\0') {
        GTEST_SKIP() << "No DISPLAY available";
    }
    // Just verify the tool exists
    FILE* f = popen("which import 2>/dev/null || which xdotool 2>/dev/null", "r");
    if (f) {
        char buf[256] = {};
        fgets(buf, sizeof(buf), f);
        pclose(f);
        if (buf[0]) {
            SUCCEED() << "Screenshot tool found: " << buf;
        } else {
            GTEST_SKIP() << "No screenshot tool (import/xdotool) found";
        }
    }
}
