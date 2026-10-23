// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_viewport_smoke.cpp — проверка что Viewport3D::Render не крашится
// на валидных данных. Требует OpenGL (X11/Wayland). Без DISPLAY — SKIP.
//
// Предотвращает баг: stale бинарник с несовпадающими размерами структур
// (старый sizeof(Segment)=988 vs новый 1692) → мусор в позициях → SEGFAULT.
// Работает в паре с CrossValidateStudioCoreSizes в test_serialization_ext.cpp.

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "viewport_3d.h"
#include "histology_panel.h"
#include "data_model.h"
#include "test_subjects/tadpole/tadpole_debug_network.h"

class ViewportSmoke : public ::testing::Test {
protected:
    GLFWwindow* window_ = nullptr;

    void SetUp() override {
        const char* display = getenv("DISPLAY");
        if (!display || !display[0]) {
            GTEST_SKIP() << "No DISPLAY — OpenGL viewport test requires X11/Wayland";
        }

        glfwSetErrorCallback([](int, const char* desc) {
            fprintf(stderr, "[GLFW] %s\n", desc);
        });

        if (!glfwInit()) {
            GTEST_SKIP() << "glfwInit() failed";
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // hidden

        window_ = glfwCreateWindow(200, 200, "viewport_smoke", nullptr, nullptr);
        if (!window_) {
            glfwTerminate();
            GTEST_SKIP() << "glfwCreateWindow failed";
        }

        glfwMakeContextCurrent(window_);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        ImGui::GetIO().DisplaySize = ImVec2(800, 600);

        // Прогрев ImGui: один кадр чтобы инициализировать внутренние структуры
        glfwPollEvents();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::EndFrame();
        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window_);
    }

    void TearDown() override {
        if (window_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(window_);
            glfwTerminate();
            window_ = nullptr;
        }
    }
};

TEST_F(ViewportSmoke, RenderWithValidSegments) {
    NetworkModel model;
    auto* seg = model.create_segment({0, 0, 0}, SegmentType::SOMA);
    seg->id = 1; seg->radius = 10.0f; seg->length = 20.0f; seg->V_m = -65.0f;
    seg->add_modifier(ModifierType::MOD_K2P_LEAK, 2.0f);

    auto* dend = model.create_segment({0, 10, 0}, SegmentType::DENDRITE);
    dend->id = 2; dend->radius = 1.0f; dend->length = 30.0f; dend->V_m = -67.0f;

    IntraConnection conn = {};
    conn.id = 0;
    conn.segment_a = 0;
    conn.segment_b = 1;
    conn.is_intracellular = 1;
    model.connections.push_back(conn);

    Viewport3D viewport;
    ASSERT_TRUE(viewport.Init()) << "Viewport3D::Init() failed";

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    HistologyConfig histo;
    viewport.Render(model, 200, 200, false, &histo);
    ImGui::EndFrame();
    ImGui::Render();
    SUCCEED() << "Viewport3D::Render() completed without crash";
}

TEST_F(ViewportSmoke, RenderWithDebugNetwork) {
    // Полная отладочная сеть головастика — проверка что рендер не падает
    // на реальных данных (4299 сегментов, 4703 соединений, 2036 синапсов)
    NetworkModel model;
    generate_debug_tadpole_network(model);

    Viewport3D viewport;
    ASSERT_TRUE(viewport.Init()) << "Viewport3D::Init() failed";

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    HistologyConfig histo;
    viewport.Render(model, 800, 600, false, &histo);
    ImGui::EndFrame();
    ImGui::Render();
    SUCCEED() << "Viewport3D::Render(debug_network) completed without crash";
}

// Проверка структуры CPG-сети: морфология, PRE/POST, связи
// НЕ использует ViewportSmoke — не нужен OpenGL
TEST(CpgStructure, GenerationAndValidation) {
    NetworkModel model;
    generate_debug_tadpole_network(model);

    // Сеть не пустая
    EXPECT_GT(model.segments.size(), 36u) << "Минимум 36 сом + отростки";
    EXPECT_GT(model.connections.size(), 0u);
    EXPECT_GT(model.synapses.size(), 0u);
    EXPECT_GT(model.neurons.size(), 0u);

    // Есть все типы сегментов
    int somas = 0, axons = 0, dends = 0, pre = 0, post = 0;
    for (auto& s : model.segments) {
        switch (s.type) {
            case SegmentType::SOMA: somas++; break;
            case SegmentType::AXON: axons++; break;
            case SegmentType::DENDRITE: dends++; break;
            case SegmentType::PRESYNAPSE: pre++; break;
            case SegmentType::POSTSYNAPSE: post++; break;
            default: break;
        }
    }
    EXPECT_GT(somas, 30) << "CPG: 36 CPG neurons expected";
    EXPECT_GT(axons, 30) << "Each neuron should have an axon";
    EXPECT_GT(dends, 20) << "dIN(3)+cIN(1)+MN(2) dendrites per neuron";
    EXPECT_GT(pre, 0) << "PRE compartments for chemical synapses";
    EXPECT_GT(post, 0) << "POST compartments for chemical synapses";

    // Синапсы используют PRE/POST (не сома→сома)
    for (auto& syn : model.synapses) {
        if (syn.type != SynapseType::CHEMICAL) continue;
        auto* pre_seg = model.find_segment(syn.pre_id);
        auto* post_seg = model.find_segment(syn.post_id);
        ASSERT_NE(pre_seg, nullptr) << "PRE segment " << syn.pre_id << " not found";
        ASSERT_NE(post_seg, nullptr) << "POST segment " << syn.post_id << " not found";
        EXPECT_EQ(pre_seg->type, SegmentType::PRESYNAPSE);
        EXPECT_EQ(post_seg->type, SegmentType::POSTSYNAPSE);
    }
    SUCCEED() << "CPG generation structure validated";
}
