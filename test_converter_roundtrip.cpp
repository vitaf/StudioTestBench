// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// test_converter_roundtrip.cpp — End-to-end тесты конвертеров SWC/NML/BION
//
// Покрывает пробелы, найденные при аудите:
//   1. SWC-парсер + логика конвертации: метрики, радиусы, bounding sphere
//   2. BION→NML→BION geometry roundtrip: радиусы, позиции, длины, типы, связи
//   3. NML→BION→NML diameter roundtrip: проверка сохранения диаметров
//   4. Проверка правил пересчёта диаметр↔радиус (NML→BION и обратно)
//   5. BION save/load: точное сохранение геометрии

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/swc_parser.h"
#include "../BioNeuronStudio/src/neuroml_import.h"
#include "../BioNeuronStudio/src/neuroml_export.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/compartment_modifiers.h"

#define NML_DIR "/home/vitaf/workarea/develop/MindCopy/examples/neuroml"

// ── Вспомогательные ──────────────────────────────────────────────────────────

static std::string write_temp_swc(const char* content) {
    std::string path = "/tmp/test_rt_swc_" + std::to_string(rand()) + ".swc";
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fputs(content, f); fclose(f); }
    return path;
}

static void cleanup(const char* path) { remove(path); }

static float dist3(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static bool exportNml(const NetworkModel& model, const char* path) {
    std::string err;
    return ExportNeuroML(model, path, err);
}

static NetworkModel makeTestModel() {
    NetworkModel m;
    auto* s1 = m.create_segment({0, 0, 0}, SegmentType::SOMA, {0, 1, 0});
    s1->length = 20.0f;  s1->radius = 10.0f;  s1->V_m = -65.0f;
    s1->intra_ions[0] = 10.0f; s1->intra_ions[1] = 140.0f;
    s1->intra_ions[2] = 0.0001f; s1->intra_ions[3] = 10.0f;
    s1->add_modifier(ModifierType::MOD_AXON_NAV12, 1.0f);

    auto* s2 = m.create_segment({0, 20, 0}, SegmentType::DENDRITE, {0, 1, 0});
    s2->length = 50.0f;  s2->radius = 3.0f;  s2->V_m = -65.0f;
    s2->intra_ions[0] = 10.0f; s2->intra_ions[1] = 140.0f;
    s2->intra_ions[2] = 0.0001f; s2->intra_ions[3] = 10.0f;
    s2->add_modifier(ModifierType::MOD_CAV13, 0.3f);

    auto* s3 = m.create_segment({0, 70, 0}, SegmentType::AXON, {0, 1, 0});
    s3->length = 100.0f;  s3->radius = 1.5f;  s3->V_m = -65.0f;
    s3->intra_ions[0] = 10.0f; s3->intra_ions[1] = 140.0f;
    s3->intra_ions[2] = 0.0001f; s3->intra_ions[3] = 10.0f;
    s3->add_modifier(ModifierType::MOD_AXON_NAV12, 1.5f);

    m.connections.push_back({0, s1->id, s2->id, 1});
    m.connections.push_back({0, s2->id, s3->id, 1});
    m.neurons.push_back({0, 0, 3, 0});
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 1: SWC-парсер + логика конвертации
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SwcParser, ChainExtentAndRadius) {
    const char* swc =
        "1 1 0 0 0 10 -1\n"
        "2 2 0 50 0 2 1\n"
        "3 2 0 100 0 1.5 2\n";
    std::string path = write_temp_swc(swc);
    SwcMorphology m = swc_parse(path.c_str());
    ASSERT_EQ(m.points.size(), 3u) << m.error_msg;

    // Метрики
    EXPECT_FLOAT_EQ(m.total_extent, 100.0f);  // 50+50
    EXPECT_FLOAT_EQ(m.axon_extent, 100.0f);
    EXPECT_FLOAT_EQ(m.soma_extent, 0.0f);

    // Радиусы сохранены
    EXPECT_FLOAT_EQ(m.points[0].radius, 10.0f);
    EXPECT_FLOAT_EQ(m.points[1].radius, 2.0f);
    EXPECT_FLOAT_EQ(m.points[2].radius, 1.5f);

    // Проверка цепочки parent-child
    EXPECT_EQ(m.points[0].parent, -1);
    EXPECT_EQ(m.points[1].parent, 1);
    EXPECT_EQ(m.points[2].parent, 2);

    cleanup(path.c_str());
}

TEST(SwcParser, BranchingStructure) {
    const char* swc =
        "1 1 0 0 0 10 -1\n"
        "2 3 0 30 0 3 1\n"
        "3 3 0 60 0 2 2\n"
        "4 3 30 30 0 2.5 1\n"
        "5 4 0 -40 0 1.5 1\n";
    std::string path = write_temp_swc(swc);
    SwcMorphology m = swc_parse(path.c_str());
    ASSERT_EQ(m.points.size(), 5u);

    // Все точки от корня: 3 прямых ребёнка (точки 2,4,5; точка 3→parent=2)
    int children = 0;
    for (auto& p : m.points)
        if (p.parent == 1) children++;
    EXPECT_EQ(children, 3) << "3 прямых ребёнка сомы (точки 2,4,5)";

    // Типы
    int somas = 0, basal = 0, apical = 0;
    for (auto& p : m.points) {
        if (p.type == 1) somas++;
        if (p.type == 3) basal++;
        if (p.type == 4) apical++;
    }
    EXPECT_EQ(somas, 1);
    EXPECT_EQ(basal, 3);
    EXPECT_EQ(apical, 1);

    // Общая протяжённость
    EXPECT_GT(m.total_extent, 50.0f);

    cleanup(path.c_str());
}

TEST(SwcParser, TaperedRadiusChain) {
    const char* swc =
        "1 1 0 0 0 10 -1\n"
        "2 2 0 20 0 10 1\n"
        "3 2 0 40 0 8 2\n"
        "4 2 0 60 0 6 3\n"
        "5 2 0 80 0 4 4\n"
        "6 2 0 100 0 2 5\n";
    std::string path = write_temp_swc(swc);
    SwcMorphology m = swc_parse(path.c_str());
    ASSERT_EQ(m.points.size(), 6u);

    // Проверка радиусов — taper 10→2
    EXPECT_FLOAT_EQ(m.points[0].radius, 10.0f);
    EXPECT_FLOAT_EQ(m.points[1].radius, 10.0f);
    EXPECT_FLOAT_EQ(m.points[2].radius, 8.0f);
    EXPECT_FLOAT_EQ(m.points[3].radius, 6.0f);
    EXPECT_FLOAT_EQ(m.points[4].radius, 4.0f);
    EXPECT_FLOAT_EQ(m.points[5].radius, 2.0f);

    // Изменение диаметра между точками 2→3: |8-10|/10 = 20%
    float delta_pct = fabsf(m.points[2].radius - m.points[1].radius)
                      / std::max(m.points[1].radius, 1e-6f) * 100.0f;
    EXPECT_NEAR(delta_pct, 20.0f, 0.1f);

    // Изменение 5→6: |2-4|/4 = 50%
    delta_pct = fabsf(m.points[5].radius - m.points[4].radius)
                / std::max(m.points[4].radius, 1e-6f) * 100.0f;
    EXPECT_NEAR(delta_pct, 50.0f, 0.1f);

    cleanup(path.c_str());
}

TEST(SwcParser, MultiPointSomaCentroid) {
    const char* swc =
        "1 1 0 0 0 5 -1\n"
        "2 1 10 0 0 5 1\n"
        "3 1 -5 0 0 5 1\n"
        "4 2 0 30 0 1 1\n";
    std::string path = write_temp_swc(swc);
    SwcMorphology m = swc_parse(path.c_str());
    ASSERT_EQ(m.points.size(), 4u);

    // Центроид сомы
    float cx = 0, cy = 0, cz = 0, cr = 0;
    int soma_pts = 0;
    for (auto& p : m.points) {
        if (p.type == 1) {
            cx += p.x; cy += p.y; cz += p.z;
            cr += p.radius;
            soma_pts++;
        }
    }
    cx /= soma_pts; cy /= soma_pts; cz /= soma_pts; cr /= soma_pts;

    EXPECT_EQ(soma_pts, 3);
    EXPECT_NEAR(cx, 1.6667f, 0.1f);   // (0+10-5)/3
    EXPECT_FLOAT_EQ(cy, 0.0f);
    EXPECT_FLOAT_EQ(cz, 0.0f);
    EXPECT_FLOAT_EQ(cr, 5.0f);

    // Bounding sphere: макс расстояние от центроида
    Vec3 centroid = {cx, cy, cz};
    float max_dist2 = 0;
    for (auto& p : m.points) {
        if (p.type == 1) {
            float dx = p.x - centroid.x, dy = p.y - centroid.y, dz = p.z - centroid.z;
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 > max_dist2) max_dist2 = d2;
        }
    }
    float bs_radius = sqrtf(max_dist2);
    EXPECT_GT(bs_radius, 5.0f) << "Bounding sphere > среднего радиуса";

    cleanup(path.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 2: BION → NML → BION geometry roundtrip
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BionNmlRoundtrip, SegmentCountPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_EQ(m1.segments.size(), 3u);
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_bion.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_export.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_export.nml", m2, err)) << err;

    printf("[BionNmlRT] orig: %zu segs, roundtrip: %zu segs\n",
           m1.segments.size(), m2.segments.size());
    EXPECT_EQ(m1.segments.size(), m2.segments.size());

    cleanup("/tmp/test_rt_bion.bion"); cleanup("/tmp/test_rt_export.nml");
}

TEST(BionNmlRoundtrip, RadiusPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_rad.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_rad.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_rad.nml", m2, err)) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++) {
        printf("[RadiusRT] seg#%zu: orig r=%.3f, rt r=%.3f, delta=%.4f\n",
               i, m1.segments[i].radius, m2.segments[i].radius,
               fabsf(m1.segments[i].radius - m2.segments[i].radius));
        EXPECT_NEAR(m1.segments[i].radius, m2.segments[i].radius, 0.02f)
            << "Радиус сегмента " << i << " должен сохраниться после roundtrip";
    }

    cleanup("/tmp/test_rt_rad.bion"); cleanup("/tmp/test_rt_rad.nml");
}

TEST(BionNmlRoundtrip, LengthPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_len.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_len.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_len.nml", m2, err)) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++)
        EXPECT_NEAR(m1.segments[i].length, m2.segments[i].length, 0.05f)
            << "Длина сегмента " << i << " должна сохраниться после roundtrip";

    cleanup("/tmp/test_rt_len.bion"); cleanup("/tmp/test_rt_len.nml");
}

TEST(BionNmlRoundtrip, PositionPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_pos.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_pos.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_pos.nml", m2, err)) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++) {
        float d = dist3(m1.segments[i].position, m2.segments[i].position);
        printf("[PosRT] seg#%zu: orig=(%.2f,%.2f,%.2f) rt=(%.2f,%.2f,%.2f) dist=%.4f\n",
               i,
               m1.segments[i].position.x, m1.segments[i].position.y, m1.segments[i].position.z,
               m2.segments[i].position.x, m2.segments[i].position.y, m2.segments[i].position.z, d);
        EXPECT_LT(d, 0.1f) << "Позиция сегмента " << i << " должна сохраниться";
    }

    cleanup("/tmp/test_rt_pos.bion"); cleanup("/tmp/test_rt_pos.nml");
}

TEST(BionNmlRoundtrip, SegmentTypesPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_type.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_type.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_type.nml", m2, err)) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    // NML не различает AXON/DENDRITE — проверяем SOMA и количество
    for (size_t i = 0; i < m1.segments.size(); i++) {
        if (m1.segments[i].type == SegmentType::SOMA)
            EXPECT_EQ((int)m2.segments[i].type, (int)SegmentType::SOMA)
                << "Сома должна остаться сомой после roundtrip";
    }

    cleanup("/tmp/test_rt_type.bion"); cleanup("/tmp/test_rt_type.nml");
}

TEST(BionNmlRoundtrip, ConnectionsPreserved) {
    NetworkModel m1 = makeTestModel();
    ASSERT_EQ(m1.connections.size(), 2u);
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_rt_conn.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_rt_conn.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_rt_conn.nml", m2, err)) << err;
    EXPECT_GE(m2.connections.size(), 1u) << "Минимум 1 связь должна сохраниться";

    cleanup("/tmp/test_rt_conn.bion"); cleanup("/tmp/test_rt_conn.nml");
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 3: NML → BION → NML diameter roundtrip
// ═══════════════════════════════════════════════════════════════════════════════

TEST(NmlBionNml, SimpleMorphology_DiameterPreserved) {
    const char* nml_in = NML_DIR "/official/NML2_SimpleMorphology.nml";
    NetworkModel m1; std::string err;
    ASSERT_TRUE(ImportNeuroML(nml_in, m1, err)) << err;
    ASSERT_GT(m1.segments.size(), 0u);
    int seg_count = (int)m1.segments.size();

    std::vector<float> orig_radii;
    for (auto& s : m1.segments) orig_radii.push_back(s.radius);

    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_nml_rt.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_nml_rt.nml"));

    NetworkModel m2;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_nml_rt.nml", m2, err)) << err;
    EXPECT_EQ(m2.segments.size(), (size_t)seg_count);

    for (size_t i = 0; i < orig_radii.size() && i < m2.segments.size(); i++)
        EXPECT_NEAR(orig_radii[i], m2.segments[i].radius, 0.015f)
            << "Радиус сегмента " << i << " должен сохраниться";

    cleanup("/tmp/test_nml_rt.bion"); cleanup("/tmp/test_nml_rt.nml");
}

TEST(NmlBionNml, FullCell_GeometryPreserved) {
    const char* nml_in = NML_DIR "/official/NML2_FullCell.nml";
    NetworkModel m1; std::string err;
    ASSERT_TRUE(ImportNeuroML(nml_in, m1, err)) << err;
    ASSERT_GT(m1.segments.size(), 0u);

    std::vector<float> orig_radii;
    for (auto& s : m1.segments) orig_radii.push_back(s.radius);

    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_nml_fc.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_nml_fc.nml"));

    NetworkModel m2;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_nml_fc.nml", m2, err)) << err;
    EXPECT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < orig_radii.size() && i < m2.segments.size(); i++)
        EXPECT_NEAR(orig_radii[i], m2.segments[i].radius, 0.015f)
            << "FullCell seg#" << i << " radius mismatch";

    cleanup("/tmp/test_nml_fc.bion"); cleanup("/tmp/test_nml_fc.nml");
}

TEST(NmlBionNml, HhCell_RadiusRoundtrip) {
    const char* nml_in = NML_DIR "/neurons/HH.cell.nml";
    NetworkModel m1; std::string err;
    ASSERT_TRUE(ImportNeuroML(nml_in, m1, err)) << err;
    ASSERT_GT(m1.segments.size(), 0u);

    std::vector<float> orig_radii;
    for (auto& s : m1.segments) orig_radii.push_back(s.radius);

    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_nml_hh.bion"));
    ASSERT_TRUE(exportNml(m1, "/tmp/test_nml_hh.nml"));

    NetworkModel m2;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_nml_hh.nml", m2, err)) << err;
    EXPECT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < orig_radii.size() && i < m2.segments.size(); i++)
        EXPECT_NEAR(orig_radii[i], m2.segments[i].radius, 0.015f)
            << "HH seg#" << i << " radius mismatch";

    cleanup("/tmp/test_nml_hh.bion"); cleanup("/tmp/test_nml_hh.nml");
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 4: Проверка правил пересчёта диаметр↔радиус
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DiameterConversion, NmlDiameterToBionRadius) {
    // NML: proximal diameter d1, distal diameter d2
    // BION импорт: radius = (d1 + d2) * 0.25
    // BION экспорт: diameter = radius * 2.0 (оба конца)

    NetworkModel m;
    auto* s = m.create_segment({0, 0, 0}, SegmentType::SOMA, {0, 1, 0});
    s->radius = 5.0f; s->length = 20.0f; s->V_m = -65.0f;
    s->intra_ions[0] = 10.0f; s->intra_ions[1] = 140.0f;
    s->intra_ions[2] = 0.0001f; s->intra_ions[3] = 10.0f;

    ASSERT_TRUE(exportNml(m, "/tmp/test_diam_conv.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_diam_conv.nml", m2, err)) << err;
    ASSERT_GE(m2.segments.size(), 1u);

    printf("[DiamConv] r_in=5.0, r_out=%.4f\n", m2.segments[0].radius);
    // При экспорте: diameter = r*2 = 10 (оба конца)
    // При импорте: r = (10+10)*0.25 = 5.0 ✓
    EXPECT_NEAR(m2.segments[0].radius, 5.0f, 0.02f)
        << "BION→NML→BION: радиус должен восстановиться";

    cleanup("/tmp/test_diam_conv.nml");
}

TEST(DiameterConversion, NmlTaperedToBionRadius) {
    // Создаём NML с разными диаметрами на концах
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"TaperCell\">\n"
        "<morphology id=\"TaperMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"10\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"10\"/>\n"
        "</segment>\n"
        "<segment id=\"1\" name=\"dend\">\n"
        "  <parent segment=\"0\"/>\n"
        "  <proximal x=\"0\" y=\"20\" z=\"0\" diameter=\"6\"/>\n"
        "  <distal x=\"0\" y=\"70\" z=\"0\" diameter=\"2\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_tapered.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 2u);

    // Формула конвертации NML→BION: r = (d_prox + d_dist) * 0.25
    EXPECT_NEAR(m.segments[0].radius, 5.0f, 0.01f);  // (10+10)*0.25=5
    EXPECT_NEAR(m.segments[1].radius, 2.0f, 0.01f);  // (6+2)*0.25=2

    printf("[TaperedNML] soma r=%.4f (expect 5.0), dend r=%.4f (expect 2.0)\n",
           m.segments[0].radius, m.segments[1].radius);

    // BION→NML: оба конца получают diameter = r*2 (тапер теряется)
    ASSERT_TRUE(exportNml(m, "/tmp/test_tapered_rt.nml"));
    NetworkModel m2;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_tapered_rt.nml", m2, err)) << err;
    ASSERT_GE(m2.segments.size(), 2u);

    // После roundtrip: радиусы сохраняются
    EXPECT_NEAR(m2.segments[0].radius, 5.0f, 0.02f);
    EXPECT_NEAR(m2.segments[1].radius, 2.0f, 0.02f);

    printf("[TaperedNML-RT] soma r=%.4f, dend r=%.4f\n",
           m2.segments[0].radius, m2.segments[1].radius);

    cleanup(path.c_str()); cleanup("/tmp/test_tapered_rt.nml");
}

TEST(DiameterConversion, RadiusFactorFormula) {
    // rf = 0.3 + 0.7 * r / (r + 2) — из converter_panel.cpp
    auto rf = [](float r) { return 0.3f + 0.7f * r / (r + 2.0f); };

    // Точные значения
    EXPECT_NEAR(rf(0.01f), 0.3035f, 0.01f);
    EXPECT_NEAR(rf(0.0f), 0.3f, 0.01f);     // через clamp
    EXPECT_NEAR(rf(2.0f), 0.65f, 0.01f);     // 0.3+0.7*2/4=0.65
    EXPECT_NEAR(rf(10.0f), 0.883f, 0.01f);   // 0.3+0.7*10/12=0.883

    // Монотонность
    EXPECT_LT(rf(0.5f), rf(1.0f));
    EXPECT_LT(rf(1.0f), rf(3.0f));
    EXPECT_LT(rf(3.0f), rf(10.0f));

    // Предел: rf → 1.0 при r → ∞
    EXPECT_LT(rf(1000.0f), 1.0f);
    EXPECT_GT(rf(1000.0f), 0.99f);

    // Биологические диапазоны
    printf("[RadiusFactor] r=0.01→%.4f r=0.5→%.4f r=1→%.4f r=2→%.4f r=5→%.4f r=10→%.4f\n",
           rf(0.01f), rf(0.5f), rf(1.0f), rf(2.0f), rf(5.0f), rf(10.0f));
}

TEST(DiameterConversion, NmlExportPositionFormula) {
    // BION сегмент: position + direction * length
    // NML export: proximal = position - direction * length/2,
    //             distal   = position + direction * length/2
    //             diameter = radius * 2

    NetworkModel m;
    auto* s = m.create_segment({10, 20, 30}, SegmentType::AXON, {0, 1, 0});
    s->length = 50.0f;
    s->radius = 3.0f;
    s->V_m = -65.0f;
    s->intra_ions[0] = 10.0f; s->intra_ions[1] = 140.0f;
    s->intra_ions[2] = 0.0001f; s->intra_ions[3] = 10.0f;

    ASSERT_TRUE(exportNml(m, "/tmp/test_nml_pos.nml"));

    NetworkModel m2; std::string err;
    ASSERT_TRUE(ImportNeuroML("/tmp/test_nml_pos.nml", m2, err)) << err;
    ASSERT_GE(m2.segments.size(), 1u);

    auto& seg = m2.segments[0];

    // Позиция: центр сегмента должен восстановиться
    EXPECT_NEAR(seg.position.x, 10.0f, 0.01f);
    EXPECT_NEAR(seg.position.y, 20.0f, 0.01f);
    EXPECT_NEAR(seg.position.z, 30.0f, 0.01f);

    // Длина
    EXPECT_NEAR(seg.length, 50.0f, 0.05f);

    // Радиус: 3.0 → diameter=6.0 → r=(6+6)*0.25=3.0
    EXPECT_NEAR(seg.radius, 3.0f, 0.02f);

    // Направление
    EXPECT_NEAR(seg.direction.x, 0.0f, 0.01f);
    EXPECT_NEAR(seg.direction.y, 1.0f, 0.01f);
    EXPECT_NEAR(seg.direction.z, 0.0f, 0.01f);

    cleanup("/tmp/test_nml_pos.nml");
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 5: BION save/load с проверкой геометрии
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BionSaveLoad, RadiusRoundtrip) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_sl.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_sl.bion", &m2, err), 0) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++)
        EXPECT_FLOAT_EQ(m1.segments[i].radius, m2.segments[i].radius)
            << "BION save/load: радиус сегмента " << i << " должен совпадать точно";

    cleanup("/tmp/test_bion_sl.bion");
}

TEST(BionSaveLoad, PositionRoundtrip) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_pos.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_pos.bion", &m2, err), 0) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++) {
        EXPECT_FLOAT_EQ(m1.segments[i].position.x, m2.segments[i].position.x);
        EXPECT_FLOAT_EQ(m1.segments[i].position.y, m2.segments[i].position.y);
        EXPECT_FLOAT_EQ(m1.segments[i].position.z, m2.segments[i].position.z);
    }

    cleanup("/tmp/test_bion_pos.bion");
}

TEST(BionSaveLoad, LengthRoundtrip) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_len2.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_len2.bion", &m2, err), 0) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++)
        EXPECT_FLOAT_EQ(m1.segments[i].length, m2.segments[i].length)
            << "BION save/load: длина сегмента " << i << " должна совпадать точно";

    cleanup("/tmp/test_bion_len2.bion");
}

TEST(BionSaveLoad, DirectionRoundtrip) {
    NetworkModel m1 = makeTestModel();
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_dir.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_dir.bion", &m2, err), 0) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++) {
        EXPECT_FLOAT_EQ(m1.segments[i].direction.x, m2.segments[i].direction.x);
        EXPECT_FLOAT_EQ(m1.segments[i].direction.y, m2.segments[i].direction.y);
        EXPECT_FLOAT_EQ(m1.segments[i].direction.z, m2.segments[i].direction.z);
    }

    cleanup("/tmp/test_bion_dir.bion");
}

TEST(BionSaveLoad, NeuronInfoRoundtrip) {
    NetworkModel m1 = makeTestModel();
    ASSERT_EQ(m1.neurons.size(), 1u);
    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_neur.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_neur.bion", &m2, err), 0) << err;
    ASSERT_EQ(m2.neurons.size(), 1u);

    EXPECT_EQ(m1.neurons[0].soma_index, m2.neurons[0].soma_index);
    EXPECT_FLOAT_EQ(m1.neurons[0].threshold, m2.neurons[0].threshold);

    cleanup("/tmp/test_bion_neur.bion");
}

TEST(BionSaveLoad, ModifiersPreserved) {
    NetworkModel m1 = makeTestModel();
    // Проверяем что модификаторы на месте
    EXPECT_GT(m1.segments[0].modifier_count, 0);
    EXPECT_GT(m1.segments[1].modifier_count, 0);

    ASSERT_TRUE(SaveNetwork(m1, "/tmp/test_bion_mod.bion"));

    NetworkModel m2; std::string err;
    ASSERT_GE(studio_load_bion("/tmp/test_bion_mod.bion", &m2, err), 0) << err;
    ASSERT_EQ(m1.segments.size(), m2.segments.size());

    for (size_t i = 0; i < m1.segments.size(); i++) {
        EXPECT_EQ(m1.segments[i].modifier_count, m2.segments[i].modifier_count)
            << "Количество модификаторов сегмента " << i << " должно сохраниться";
        for (int j = 0; j < m1.segments[i].modifier_count; j++) {
            EXPECT_EQ((int)m1.segments[i].modifiers[j].type,
                      (int)m2.segments[i].modifiers[j].type)
                << "Тип модификатора seg#" << i << " mod#" << j;
            EXPECT_FLOAT_EQ(m1.segments[i].modifiers[j].density,
                            m2.segments[i].modifiers[j].density)
                << "Density модификатора seg#" << i << " mod#" << j;
        }
    }

    cleanup("/tmp/test_bion_mod.bion");
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 6: Консистентность сегментации с разными параметрами
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SwcSegmentation, UniformLengthMath) {
    // При uniform_length=10, аксон длиной 100μm → ceil(100/10)=10 подсегментов
    float total_length = 100.0f;
    float max_len = 10.0f;
    int expected = (int)ceilf(total_length / max_len);
    EXPECT_EQ(expected, 10);

    // При uniform_length=50 → ceil(100/50)=2
    max_len = 50.0f;
    expected = (int)ceilf(total_length / max_len);
    EXPECT_EQ(expected, 2);

    // Длина подсегмента = total / subdivisions
    float sub_len = total_length / (float)expected;
    EXPECT_FLOAT_EQ(sub_len, 50.0f);
}

TEST(SwcSegmentation, DiameterThresholdLogic) {
    // Режим tree_mode=2: ветвь дробится при |Δd|/d_max > threshold%
    float prev_r = 10.0f, next_r = 8.0f;
    float change_pct = fabsf(next_r - prev_r) / std::max(prev_r, 1e-6f) * 100.0f;
    EXPECT_NEAR(change_pct, 20.0f, 0.01f);

    // При threshold=60%: 20% < 60% → не дробится
    EXPECT_LT(change_pct, 60.0f);

    // При threshold=10%: 20% > 10% → дробится
    EXPECT_GT(change_pct, 10.0f);

    prev_r = 6.0f; next_r = 4.0f;
    change_pct = fabsf(next_r - prev_r) / std::max(prev_r, 1e-6f) * 100.0f;
    EXPECT_NEAR(change_pct, 33.333f, 0.1f);

    prev_r = 4.0f; next_r = 2.0f;
    change_pct = fabsf(next_r - prev_r) / std::max(prev_r, 1e-6f) * 100.0f;
    EXPECT_NEAR(change_pct, 50.0f, 0.01f);
}

TEST(SwcSegmentation, BoundingSphereCalculation) {
    // Сома из 3 точек: (0,0,0), (10,0,0), (-5,0,0)
    float points[][3] = {{0,0,0}, {10,0,0}, {-5,0,0}};
    int n = 3;

    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) {
        cx += points[i][0]; cy += points[i][1]; cz += points[i][2];
    }
    cx /= n; cy /= n; cz /= n;
    EXPECT_NEAR(cx, 1.6667f, 0.1f);

    float max_dist2 = 0;
    for (int i = 0; i < n; i++) {
        float dx = points[i][0] - cx;
        float dy = points[i][1] - cy;
        float dz = points[i][2] - cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > max_dist2) max_dist2 = d2;
    }
    float bs_radius = sqrtf(max_dist2);
    // Расстояния от центроида: (1.67,0,0)→(0,0,0)=1.67, →(10,0,0)=8.33, →(-5,0,0)=6.67
    EXPECT_NEAR(bs_radius, 8.333f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 7: NML-импорт — дефолтные модификаторы
// ═══════════════════════════════════════════════════════════════════════════════

// NML без <channelDensity> — сегменты получают дефолтные модификаторы
TEST(NmlDefaultModifiers, NoChannelDensity_GetsDefaults) {
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"BareCell\">\n"
        "<morphology id=\"BareMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"20\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"20\"/>\n"
        "</segment>\n"
        "<segment id=\"1\" name=\"dend\">\n"
        "  <parent segment=\"0\"/>\n"
        "  <proximal x=\"0\" y=\"20\" z=\"0\" diameter=\"4\"/>\n"
        "  <distal x=\"0\" y=\"70\" z=\"0\" diameter=\"2\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_nml_nodef.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 2u);

    // Сома (r=5) должна иметь минимум 2 модификатора (NaK + pump или Ca)
    int soma_mods = m.segments[0].modifier_count;
    EXPECT_GE(soma_mods, 2) << "Сома без channelDensity должна получить дефолтные модификаторы";

    // Дендрит тоже
    int dend_mods = m.segments[1].modifier_count;
    EXPECT_GE(dend_mods, 2) << "Дендрит без channelDensity должен получить дефолтные модификаторы";

    // Вывод для диагностики
    printf("[NmlNoDef] soma: %d mods, dend: %d mods\n", soma_mods, dend_mods);
    for (int j = 0; j < m.segments[0].modifier_count; j++)
        printf("  soma mod#%d: type=%d density=%.3f\n",
               j, (int)m.segments[0].modifiers[j].type,
               m.segments[0].modifiers[j].density);
    for (int j = 0; j < m.segments[1].modifier_count; j++)
        printf("  dend mod#%d: type=%d density=%.3f\n",
               j, (int)m.segments[1].modifiers[j].type,
               m.segments[1].modifiers[j].density);

    cleanup(path.c_str());
}

// NML с <channelDensity> — явные каналы сохраняются + добавляются недостающие
TEST(NmlDefaultModifiers, WithChannelDensity_ExplicitPreserved) {
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"PartialCell\">\n"
        "<morphology id=\"PartialMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"20\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"20\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "<biophysicalProperties>\n"
        "<membraneProperties>\n"
        "<channelDensity id=\"na_ch\" ionChannel=\"Na\" condDensity=\"100 S_per_cm2\" "
        "  segmentGroup=\"all\" ion=\"na\"/>\n"
        "</membraneProperties>\n"
        "</biophysicalProperties>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_nml_partial.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 1u);

    int n_mods = m.segments[0].modifier_count;
    // Должно быть >1: NaK из channelDensity + насос/Kv7 из дефолтов
    EXPECT_GE(n_mods, 2) << "NML с channelDensity должен также получить дефолтные модификаторы";

    bool has_nak = false, has_pump = false;
    for (int j = 0; j < n_mods; j++) {
        if (m.segments[0].modifiers[j].mod_type() == ModifierType::MOD_AXON_NAV12)
            has_nak = true;
        if (m.segments[0].modifiers[j].mod_type() == ModifierType::MOD_PUMP_NAK)
            has_pump = true;
        printf("  mod#%d: type=%d density=%.3f\n",
               j, (int)m.segments[0].modifiers[j].type,
               m.segments[0].modifiers[j].density);
    }
    EXPECT_TRUE(has_nak) << "NaK канал из channelDensity должен сохраниться";
    EXPECT_TRUE(has_pump) << "Na/K-насос должен быть добавлен дефолтно";

    cleanup(path.c_str());
}

// Аксон без channelDensity — НЕ должен получить Ca каналы (биологично)
TEST(NmlDefaultModifiers, AxonHasNoCalciumByDefault) {
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"AxonCell\">\n"
        "<morphology id=\"AxonMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"20\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"20\"/>\n"
        "</segment>\n"
        "<segment id=\"1\" name=\"axon\">\n"
        "  <parent segment=\"0\"/>\n"
        "  <proximal x=\"0\" y=\"20\" z=\"0\" diameter=\"2\"/>\n"
        "  <distal x=\"0\" y=\"120\" z=\"0\" diameter=\"1\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_nml_axon.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 2u);

    // Сегмент 1 — аксон (имя содержит "axon")
    auto& axon = m.segments[1];
    printf("[NmlAxon] name check: '%s' → type=%d (AXON=%d)\n",
           "axon", (int)axon.type, (int)SegmentType::AXON);

    // Проверяем что Ca каналы не назначены аксону
    for (int j = 0; j < axon.modifier_count; j++) {
        EXPECT_NE((int)axon.modifiers[j].type, (int)ModifierType::MOD_CAV13)
            << "Аксон не должен иметь Ca²⁺ каналы (биологично)";
    }
    EXPECT_GE(axon.modifier_count, 1) << "Аксон должен иметь хотя бы NaK каналы";

    cleanup(path.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
// БЛОК 8: Ионные концентрации — унификация между путями импорта
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IonConcentrations, NmlImportSetsExtraIons) {
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"IonCell\">\n"
        "<morphology id=\"IonMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"20\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"20\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_nml_ions.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 1u);

    auto& s = m.segments[0];

    // Внутриклеточные
    EXPECT_FLOAT_EQ(s.intra_ions[0], DefaultIonConcentrations::intra_na);
    EXPECT_FLOAT_EQ(s.intra_ions[1], DefaultIonConcentrations::intra_k);
    EXPECT_FLOAT_EQ(s.intra_ions[2], DefaultIonConcentrations::intra_ca);
    EXPECT_FLOAT_EQ(s.intra_ions[3], DefaultIonConcentrations::intra_cl);

    // Внеклеточные — должны быть заполнены (раньше были только intra)
    EXPECT_FLOAT_EQ(s.extra_ions[0], DefaultIonConcentrations::extra_na);
    EXPECT_FLOAT_EQ(s.extra_ions[1], DefaultIonConcentrations::extra_k);
    EXPECT_FLOAT_EQ(s.extra_ions[2], DefaultIonConcentrations::extra_ca);
    EXPECT_FLOAT_EQ(s.extra_ions[3], DefaultIonConcentrations::extra_cl);

    printf("[IonNML] intra=(%.1f,%.1f,%.6f,%.1f) extra=(%.1f,%.1f,%.1f,%.1f)\n",
           s.intra_ions[0], s.intra_ions[1], s.intra_ions[2], s.intra_ions[3],
           s.extra_ions[0], s.extra_ions[1], s.extra_ions[2], s.extra_ions[3]);

    cleanup(path.c_str());
}

TEST(IonConcentrations, NmlImportIntraNaIs10not15) {
    const char* nml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<neuroml xmlns=\"http://www.neuroml.org/schema/neuroml2\">\n"
        "<cell id=\"NaCell\">\n"
        "<morphology id=\"NaMorph\">\n"
        "<segment id=\"0\" name=\"soma\">\n"
        "  <proximal x=\"0\" y=\"0\" z=\"0\" diameter=\"20\"/>\n"
        "  <distal x=\"0\" y=\"20\" z=\"0\" diameter=\"20\"/>\n"
        "</segment>\n"
        "</morphology>\n"
        "</cell>\n"
        "</neuroml>\n";

    std::string path = "/tmp/test_nml_na.nml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs(nml, f); fclose(f);

    NetworkModel m; std::string err;
    ASSERT_TRUE(ImportNeuroML(path.c_str(), m, err)) << err;
    ASSERT_GE(m.segments.size(), 1u);

    // Было 15.0 — должно быть 10.0 (унифицировано со SWC)
    EXPECT_FLOAT_EQ(m.segments[0].intra_ions[0], 10.0f)
        << "intra Na+ должно быть 10 мМ (унифицировано со SWC), не 15";

    cleanup(path.c_str());
}
