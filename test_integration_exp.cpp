// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Интеграционные тесты: связки experiment ↔ probes ↔ model ↔ template ↔ batch
// Проверяют полные пользовательские сценарии без регрессий

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "data_model.h"
#include "experiment_data.h"
#include "experiment_manager.h"
#include "probe_panel.h"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

// ── Вспомогательные: создать тестовую .bion модель ────────────────────────
static std::string create_test_bion(const char* dir, const char* name) {
    mkdir(dir, 0755);
    std::string path = std::string(dir) + "/" + name + ".bion";
    // Пишем минимальный .bion v4 файл напрямую
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
    uint32_t magic = 0x4E4F4942; // "BION"
    uint32_t ver = 4;
    uint32_t flags = 0;
    uint32_t num_seg = 5, num_conn = 0, num_syn = 0, num_mod = 0;
    fwrite(&magic, 4, 1, f);
    fwrite(&ver, 4, 1, f);
    fwrite(&flags, 4, 1, f);
    fwrite(&num_seg, 4, 1, f); fwrite(&num_conn, 4, 1, f); fwrite(&num_syn, 4, 1, f); fwrite(&num_mod, 4, 1, f);
    // 5 пустых сегментов (BionSegment = 932 bytes each)
    char zero_seg[932] = {};
    for (int i = 0; i < 5; i++) {
        uint32_t sid = (uint32_t)(i + 1);
        memcpy(zero_seg, &sid, 4); // id
        fwrite(zero_seg, 1, sizeof(zero_seg), f);
    }
    fclose(f);
    return path;
}

int main() {
    const char* dir = "/tmp/test_integration_exp";
    mkdir(dir, 0755);

    // ═══════════════════════════════════════════════════════════════════
    SEC("1. ConfigToProbe: ELECTRODE full round-trip");
    // ═══════════════════════════════════════════════════════════════════
    {
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ELECTRODE;
        cfg.segment_id = 42;
        strncpy(cfg.label, "E_Test", 31);
        cfg.record_vm = true;
        cfg.record_ions[0] = true; cfg.record_ions[4] = true; // Na + Mg
        cfg.color[0] = 0.9f; cfg.color[1] = 0.1f; cfg.color[2] = 0.1f;
        cfg.channels = 3;
        cfg.stim_enabled = true;
        cfg.stim_amplitude_pA = 250.0f;
        cfg.stim_duration_ms = 12.0f;
        cfg.stim_delay_ms = 4.0f;
        cfg.stim_type = 1; // ramp

        Probe p = ExperimentManager::ConfigToProbe(cfg);
        CHK(p.type == ProbeType::ELECTRODE, "type");
        CHK(p.segment_id == 42u, "seg_id");
        CHK(strcmp(p.label, "E_Test") == 0, "label=%s", p.label);
        CHK(p.record_vm, "record_vm");
        CHK(p.record_ions[0] && !p.record_ions[1] && !p.record_ions[2] && !p.record_ions[3] && p.record_ions[4],
            "record_ions: Na+Mg only");
        CHK(fabsf(p.stim_amplitude_pA - 250.0f) < 0.01f, "stim_amp=%.1f", p.stim_amplitude_pA);
        CHK(fabsf(p.stim_duration_ms - 12.0f) < 0.01f, "stim_dur=%.1f", p.stim_duration_ms);
        CHK(fabsf(p.stim_delay_ms - 4.0f) < 0.01f, "stim_delay=%.1f", p.stim_delay_ms);
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("2. ConfigToProbe: CANNULE with LIGAND");
    // ═══════════════════════════════════════════════════════════════════
    {
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::CANNULE;
        cfg.cannula_placement = CannulaPlacement::EXTRACELLULAR;
        cfg.inject_enabled = true;
        cfg.inject_is_ion = false; // ligand
        cfg.inject_substance = 5;  // adenosine
        cfg.inject_amount = 3.5f;

        Probe p = ExperimentManager::ConfigToProbe(cfg);
        CHK(p.type == ProbeType::CANNULE, "type");
        CHK(p.cannula_placement == CannulaPlacement::EXTRACELLULAR, "extracellular");
        CHK(p.substance_type == CannulaSubstance::LIGAND, "ligand");
        CHK(p.inject_substance_idx == 5, "adenosine idx=%d", p.inject_substance_idx);
        CHK(fabsf(p.inject_amount_mM - 3.5f) < 0.01f, "amount=%.1f", p.inject_amount_mM);
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("3. ConfigToProbe: ION_SELECTIVE");
    // ═══════════════════════════════════════════════════════════════════
    {
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ION_SELECTIVE;
        cfg.segment_id = 7;
        cfg.record_vm = false;
        cfg.record_ions[1] = true; // K+
        cfg.record_ions[2] = true; // Ca2+
        cfg.channels = 2;

        Probe p = ExperimentManager::ConfigToProbe(cfg);
        CHK(p.type == ProbeType::ION_SELECTIVE, "type");
        CHK(!p.record_vm, "no Vm");
        CHK(p.record_ions[1] && p.record_ions[2], "K+Ca recorded");
        CHK(!p.stim_enabled && !p.inject_enabled, "no stim/inject by default");
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("4. ApplyExperimentSetup: multiple probes on model");
    // ═══════════════════════════════════════════════════════════════════
    {
        NetworkModel m;
        for (int i = 0; i < 10; i++) {
            SegmentSnapshot s{}; s.id = (uint32_t)(i + 1); m.segments.push_back(s);
        }

        ExperimentData data;
        for (int i = 0; i < 3; i++) {
            ExperimentProbeConfig cfg;
            cfg.type = (i == 0) ? ProbeType::ELECTRODE : ProbeType::CANNULE;
            cfg.segment_id = (uint32_t)(i * 3 + 1);
            snprintf(cfg.label, sizeof(cfg.label), "Probe_%d", i);
            cfg.record_vm = (i == 0);
            cfg.channels = cfg.record_vm ? 1u : 0u;
            if (i == 0) { cfg.stim_enabled = true; cfg.stim_amplitude_pA = 100.0f; }
            if (i == 1) { cfg.inject_enabled = true; cfg.inject_is_ion = true; cfg.inject_substance = 0; }
            data.probes.push_back(cfg);
        }

        ExperimentManager::ApplyExperimentSetup(m, data);
        CHK(m.probes.size() == 3, "probe count=%zu", m.probes.size());
        CHK(m.probes[0].type == ProbeType::ELECTRODE, "probe0 electrode");
        CHK(m.probes[1].type == ProbeType::CANNULE, "probe1 cannula");
        CHK(m.probes[0].stim_enabled && fabsf(m.probes[0].stim_amplitude_pA - 100.0f) < 0.01f,
            "probe0 stim preserved");
        CHK(m.probes[1].inject_enabled && m.probes[1].substance_type == CannulaSubstance::ION,
            "probe1 inject preserved");
        // probe IDs должны быть уникальными
        CHK(m.probes[0].id != m.probes[1].id && m.probes[1].id != m.probes[2].id,
            "unique probe IDs: %u %u %u", m.probes[0].id, m.probes[1].id, m.probes[2].id);
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("5. ApplyExperimentSetup: empty probes (no-op)");
    // ═══════════════════════════════════════════════════════════════════
    {
        NetworkModel m;
        SegmentSnapshot s{}; s.id = 1; m.segments.push_back(s);
        Probe old_p; old_p.id = 99; old_p.type = ProbeType::ELECTRODE;
        m.probes.push_back(old_p);

        ExperimentData data; // no probes
        ExperimentManager::ApplyExperimentSetup(m, data);
        CHK(m.probes.empty(), "old probes cleared: %zu", m.probes.size());
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("6. ResolveBionPath: absolute path → near .bexp → model_name");
    // ═══════════════════════════════════════════════════════════════════
    {
        std::string bion_path = create_test_bion(dir, "integration_model");
        CHK(!bion_path.empty(), "test bion created: %s", bion_path.c_str());

        ExperimentMeta meta;
        meta.bion_file_path = bion_path;
        meta.model_name = "integration_model.bion";

        // 6a: absolute path
        std::string r1 = ExperimentManager::ResolveBionPath(meta, nullptr);
        CHK(r1 == bion_path, "resolve by absolute: %s", r1.c_str());

        // 6b: broken absolute → fallback to near .bexp
        meta.bion_file_path = "/nonexistent/broken.bion";
        std::string bexp = std::string(dir) + "/test.bexp";
        std::string r2 = ExperimentManager::ResolveBionPath(meta, bexp.c_str());
        CHK(r2 == bion_path, "resolve near .bexp: %s", r2.c_str());

        // 6c: both broken → fallback to model_name (if exists in CWD or by name)
        std::string r3 = ExperimentManager::ResolveBionPath(meta, "/nonexistent/other.bexp");
        // Не можем гарантировать что найдёт, но не должен вернуть битый путь
        CHK(r3 != "/nonexistent/broken.bion", "doesn't return broken path");
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("7. saveAsTemplate → loadFromFile round-trip");
    // ═══════════════════════════════════════════════════════════════════
    {
        const char* tmpl = "/tmp/test_integration_exp/template.bexp_template";

        ExperimentData src;
        src.meta.name = "IntegrationTemplate";
        src.meta.description = "Test template";
        src.meta.model_name = "model.bion";
        src.meta.bion_file_path = "/tmp/model.bion";
        src.meta.cell_type = "Pyramidal";
        src.meta.probe_count = 2;
        src.meta.tick_count = 100; // будет заменён на 0
        src.frame_stride = 3;
        src.frames = {1,2,3,4,5,6,7,8,9};

        ExperimentProbeConfig e1; e1.type = ProbeType::ELECTRODE;
        e1.segment_id = 10; strncpy(e1.label, "E1", 31); e1.record_vm = true; e1.channels = 1;
        e1.stim_enabled = true; e1.stim_amplitude_pA = 200.0f;
        src.probes.push_back(e1);

        ExperimentProbeConfig c1; c1.type = ProbeType::CANNULE;
        c1.segment_id = 20; strncpy(c1.label, "C1", 31); c1.channels = 2;
        c1.record_ions[0] = true; c1.record_ions[1] = true;
        c1.inject_enabled = true; c1.inject_is_ion = false; c1.inject_substance = 3;
        src.probes.push_back(c1);

        ExperimentManager mgr;
        CHK(mgr.saveAsTemplate(tmpl, src), "save template");

        // Загружаем обратно
        ExperimentData loaded; std::string err;
        CHK(mgr.loadFromFile(tmpl, loaded, err), "load template: %s", err.c_str());
        CHK(loaded.meta.tick_count == 0u, "tick_count=0 after load: %u", loaded.meta.tick_count);
        CHK(loaded.frames.empty(), "frames empty");
        CHK(loaded.events.empty(), "events empty");
        CHK(loaded.probes.size() == 2, "probes preserved: %zu", loaded.probes.size());
        CHK(strcmp(loaded.probes[0].label, "E1") == 0, "E1 label");
        CHK(loaded.probes[0].stim_enabled, "E1 stim preserved");
        CHK(strcmp(loaded.probes[1].label, "C1") == 0, "C1 label");
        CHK(loaded.probes[1].inject_enabled, "C1 inject preserved");
        CHK(!loaded.probes[1].inject_is_ion, "C1 is ligand");
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("8. saveToFile → loadFromFile round-trip (full .bexp)");
    // ═══════════════════════════════════════════════════════════════════
    {
        const char* bexp = "/tmp/test_integration_exp/full_test.bexp";

        // Создаём через ExperimentManager (эмулируем реальный flow)
        NetworkModel m;
        for (int i = 0; i < 5; i++) {
            SegmentSnapshot s{}; s.id = (uint32_t)(i + 1); m.segments.push_back(s);
        }
        std::string bion_path = create_test_bion(dir, "full_test_model");
        CHK(!bion_path.empty(), "test bion for save→load");
        m.bion_filename = bion_path;  // реальный путь — realpath сработает

        Probe pr; pr.type = ProbeType::ELECTRODE; pr.segment_id = 1;
        pr.stim_enabled = true; pr.stim_amplitude_pA = 150.0f; pr.stim_duration_ms = 5.0f;
        pr.stim_delay_ms = 2.0f;
        pr.record_vm = true;
        m.probes.push_back(pr);

        Probe pr2; pr2.type = ProbeType::CANNULE; pr2.segment_id = 3;
        pr2.inject_enabled = true; pr2.substance_type = CannulaSubstance::ION;
        pr2.inject_substance_idx = 0; pr2.inject_amount_mM = 10.0f;
        pr2.record_ions[0] = true;
        m.probes.push_back(pr2);

        ExperimentManager mgr;
        CHK(mgr.startRecording(m, "FullTest", "Integration save→load", "Pyramidal"),
            "startRecording");

        // Эмулируем запись вручную (без ProbeManager, чтобы не тянуть ImGui)
        // recordTick читает из probe_manager.recordings — заполняем их руками
        ProbeManager pm;
        pm.recordings.resize(m.probes.size());
        for (size_t pi = 0; pi < m.probes.size(); pi++) {
            pm.recordings[pi].push(-65.0f + (float)pi, m.segments[m.probes[pi].segment_id - 1].intra_ions);
        }
        for (int t = 0; t < 10; t++) {
            mgr.recordTick(m, pm, (uint32_t)t);
        }
        mgr.stopRecording();
        CHK(mgr.saveToFile(bexp), "save .bexp");

        // Загружаем
        ExperimentData loaded; std::string err;
        CHK(mgr.loadFromFile(bexp, loaded, err), "load .bexp: %s", err.c_str());
        CHK(loaded.meta.name == "FullTest", "name preserved");
        CHK(loaded.meta.tick_count == 10u, "tick_count=%u", loaded.meta.tick_count);
        CHK(!loaded.meta.bion_file_path.empty(), "bion_file_path saved");
        CHK(loaded.probes.size() == 2, "probe count=%zu", loaded.probes.size());

        // Проверяем первый проб (ELECTRODE с стимуляцией)
        CHK(loaded.probes[0].type == ProbeType::ELECTRODE, "p0 electrode");
        CHK(loaded.probes[0].stim_enabled, "p0 stim_enabled");
        CHK(fabsf(loaded.probes[0].stim_amplitude_pA - 150.0f) < 0.01f,
            "p0 stim_amp=%.1f", loaded.probes[0].stim_amplitude_pA);
        CHK(fabsf(loaded.probes[0].stim_delay_ms - 2.0f) < 0.01f,
            "p0 stim_delay=%.1f", loaded.probes[0].stim_delay_ms);

        // Проверяем второй проб (CANNULE с инжекцией)
        CHK(loaded.probes[1].type == ProbeType::CANNULE, "p1 cannula");
        CHK(loaded.probes[1].inject_enabled, "p1 inject_enabled");
        CHK(loaded.probes[1].inject_is_ion, "p1 inject_is_ion=true");
        CHK(loaded.probes[1].inject_substance == 0, "p1 inject Na");
        CHK(fabsf(loaded.probes[1].inject_amount - 10.0f) < 0.01f,
            "p1 inject_amount=%.1f", loaded.probes[1].inject_amount);

        // Проверяем данные кадров
        CHK(loaded.frames.size() == 10u * loaded.frame_stride,
            "frames size=%zu stride=%u", loaded.frames.size(), loaded.frame_stride);
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("9. v1 backward compatibility (no bion_file_path)");
    // ═══════════════════════════════════════════════════════════════════
    {
        // Создаём v1-совместимый .bexp вручную (без новых полей)
        const char* v1_path = "/tmp/test_integration_exp/v1_compat.bexp";
        const char* json =
            "{\n"
            "  \"name\": \"V1Test\",\n"
            "  \"description\": \"No new fields\",\n"
            "  \"date\": \"2026-01-01\",\n"
            "  \"model_name\": \"old_model.bion\",\n"
            "  \"cell_type\": \"Interneuron\",\n"
            "  \"probe_count\": 1,\n"
            "  \"tick_count\": 5,\n"
            "  \"probes\": [\n"
            "    {\"type\":\"ELECTRODE\",\"segment_id\":1,\"label\":\"OldProbe\","
            "\"record_vm\":true,\"record_ions\":[false,false,false,false],"
            "\"channels\":1,\"placement\":-1,\"inject_enabled\":false,"
            "\"inject_substance\":0,\"inject_amount\":0.0,"
            "\"cannula_placement\":\"INTRACELLULAR\"}\n"
            "  ]\n"
            "}\n";

        FILE* f = fopen(v1_path, "wb");
        uint32_t magic = BEXP_MAGIC, ver = 1, jl = (uint32_t)strlen(json);
        fwrite(&magic, 4, 1, f); fwrite(&ver, 4, 1, f); fwrite(&jl, 4, 1, f);
        fwrite(json, 1, jl, f);
        uint32_t fc = 5, fs = 2, fb = 40;
        fwrite(&fc, 4, 1, f); fwrite(&fs, 4, 1, f); fwrite(&fb, 4, 1, f);
        float frame_data[] = {0,1, 1,2, 2,3, 3,4, 4,5};
        fwrite(frame_data, 4, 10, f);
        uint32_t ne = 0; fwrite(&ne, 4, 1, f);
        fclose(f);

        ExperimentManager mgr;
        ExperimentData loaded; std::string err;
        CHK(mgr.loadFromFile(v1_path, loaded, err), "load v1 .bexp: %s", err.c_str());
        CHK(loaded.meta.name == "V1Test", "v1 name");
        CHK(loaded.meta.model_name == "old_model.bion", "v1 model_name");
        CHK(loaded.meta.bion_file_path.empty(), "v1 has no bion_file_path (expected)");
        CHK(loaded.meta.tick_count == 5u, "v1 tick_count");
        CHK(loaded.probes.size() == 1, "v1 probes");
        CHK(strcmp(loaded.probes[0].label, "OldProbe") == 0, "v1 probe label");
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("10. ApplyExperimentSetup: probe params survive round-trip");
    // ═══════════════════════════════════════════════════════════════════
    {
        // Создаём модель, записываем эксперимент, загружаем
        NetworkModel m;
        for (int i = 0; i < 5; i++) {
            SegmentSnapshot s{}; s.id = (uint32_t)(i + 1); m.segments.push_back(s);
        }
        std::string bp10 = create_test_bion(dir, "roundtrip_model");
        m.bion_filename = bp10;

        // Добавляем проб со всеми параметрами
        Probe pr; pr.type = ProbeType::ELECTRODE; pr.segment_id = 3;
        pr.stim_enabled = true; pr.stim_amplitude_pA = 175.0f;
        pr.stim_duration_ms = 8.0f; pr.stim_delay_ms = 3.0f;
        pr.record_vm = true; pr.record_ions[2] = true;  // Ca
        m.probes.push_back(pr);

        ExperimentManager mgr;
        mgr.startRecording(m, "RoundTrip", "", "Test");
        ProbeManager pm; pm.recordings.resize(m.probes.size());
        for (size_t pi = 0; pi < m.probes.size(); pi++) {
            pm.recordings[pi].push(-65.0f, m.segments[0].intra_ions);
        }
        for (int t = 0; t < 5; t++) { mgr.recordTick(m, pm, (uint32_t)t); }
        mgr.stopRecording();
        mgr.saveToFile("/tmp/test_integration_exp/roundtrip.bexp");

        ExperimentData loaded; std::string err;
        mgr.loadFromFile("/tmp/test_integration_exp/roundtrip.bexp", loaded, err);

        // Очищаем модель и применяем пробы из загруженного
        m.probes.clear();
        ExperimentManager::ApplyExperimentSetup(m, loaded);

        CHK(m.probes.size() == 1, "probe restored");
        CHK(m.probes[0].segment_id == 3u, "seg_id restored");
        CHK(m.probes[0].stim_enabled, "stim restored");
        CHK(fabsf(m.probes[0].stim_amplitude_pA - 175.0f) < 0.01f,
            "stim_amp restored: %.1f", m.probes[0].stim_amplitude_pA);
        CHK(fabsf(m.probes[0].stim_delay_ms - 3.0f) < 0.01f,
            "stim_delay restored: %.1f", m.probes[0].stim_delay_ms);
        CHK(m.probes[0].record_ions[2], "Ca record restored");
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("11. Multiple ApplyExperimentSetup calls (idempotency)");
    // ═══════════════════════════════════════════════════════════════════
    {
        NetworkModel m;
        for (int i = 0; i < 5; i++) {
            SegmentSnapshot s{}; s.id = (uint32_t)(i + 1); m.segments.push_back(s);
        }
        ExperimentData data;
        ExperimentProbeConfig cfg; cfg.type = ProbeType::ELECTRODE;
        cfg.segment_id = 2; strncpy(cfg.label, "Test", 31);
        cfg.record_vm = true; cfg.channels = 1;
        data.probes.push_back(cfg);

        // Первый вызов
        ExperimentManager::ApplyExperimentSetup(m, data);
        size_t first_count = m.probes.size();
        CHK(first_count == 1, "first apply: %zu probes", first_count);

        // Второй вызов — старые пробы удаляются, новые добавляются
        ExperimentManager::ApplyExperimentSetup(m, data);
        CHK(m.probes.size() == 1, "second apply: %zu probes (no duplicates)", m.probes.size());
    }

    // ═══════════════════════════════════════════════════════════════════
    SEC("12. saveAsTemplate preserves all new fields");
    // ═══════════════════════════════════════════════════════════════════
    {
        const char* tmpl = "/tmp/test_integration_exp/new_fields_template.bexp_template";
        ExperimentData src;
        src.meta.name = "NewFields";
        src.meta.bion_file_path = "/abs/path/model.bion";

        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ELECTRODE; cfg.segment_id = 1;
        strncpy(cfg.label, "Full", 31);
        cfg.record_vm = true; cfg.record_ions[4] = true; // Mg
        cfg.channels = 2;
        cfg.stim_enabled = true; cfg.stim_amplitude_pA = 300.0f;
        cfg.stim_duration_ms = 10.0f; cfg.stim_delay_ms = 1.0f;
        cfg.stim_type = 2; // train
        cfg.inject_enabled = true; cfg.inject_is_ion = true;
        cfg.inject_substance = 2; cfg.inject_amount = 7.5f;
        src.probes.push_back(cfg);

        ExperimentManager mgr;
        CHK(mgr.saveAsTemplate(tmpl, src), "save new-fields template");

        ExperimentData loaded; std::string err;
        CHK(mgr.loadFromFile(tmpl, loaded, err), "load new-fields template");
        CHK(loaded.probes.size() == 1, "probe loaded");
        CHK(loaded.probes[0].stim_enabled, "stim_enabled");
        CHK(loaded.probes[0].stim_type == 2, "stim_type=train: %d", loaded.probes[0].stim_type);
        CHK(fabsf(loaded.probes[0].stim_delay_ms - 1.0f) < 0.01f,
            "stim_delay=%.1f", loaded.probes[0].stim_delay_ms);
        CHK(loaded.probes[0].inject_is_ion, "inject_is_ion");
        CHK(loaded.probes[0].record_ions[4], "Mg recorded");
        CHK(!loaded.meta.bion_file_path.empty(), "bion_file_path preserved");
    }

    // Очистка
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);

    int t = P + F;
    printf("\n=== INTEGRATION EXP: %d PASS, %d FAIL (%d%%) ===\n", P, F, t > 0 ? P * 100 / t : 0);
    return F > 0 ? 1 : 0;
}
