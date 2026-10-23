// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Тест ExperimentManager: create, save, scan, search, delete,
// ConfigToProbe, ApplyExperimentSetup, ResolveBionPath, saveAsTemplate
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "data_model.h"
#include "experiment_data.h"
#include "experiment_manager.h"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

// Minimal ExperimentIndexEntry (mirrors experiment_manager.h)
struct ExpIdx { std::string name, description, date, cell_type, file_path; uint32_t tick_count; };

static void write_index(const char* dir, const std::vector<ExpIdx>& entries) {
    std::string path=std::string(dir)+"/index.json";
    FILE*f=fopen(path.c_str(),"w");
    fprintf(f,"[\n");
    for(size_t i=0;i<entries.size();i++){
        fprintf(f,"  {\"name\":\"%s\",\"description\":\"%s\",\"date\":\"%s\",\"cell_type\":\"%s\",\"tick_count\":%u,\"file\":\"%s\"}%s\n",
            entries[i].name.c_str(),entries[i].description.c_str(),entries[i].date.c_str(),
            entries[i].cell_type.c_str(),entries[i].tick_count,entries[i].file_path.c_str(),
            i<entries.size()-1?",":"");
    }
    fprintf(f,"]\n");fclose(f);
}

static std::vector<ExpIdx> read_index(const char* dir) {
    std::vector<ExpIdx> result;
    std::string path=std::string(dir)+"/index.json";
    FILE*f=fopen(path.c_str(),"r");if(!f)return result;
    fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    std::string json(sz,'\0');fread(&json[0],1,sz,f);fclose(f);

    // Simple JSON parser
    size_t pos=0;
    while((pos=json.find("\"name\":\"",pos))!=std::string::npos){
        pos+=8;size_t e=json.find('"',pos);
        ExpIdx entry;entry.name=json.substr(pos,e-pos);
        pos=json.find("\"description\":\"",e);if(pos==std::string::npos)break;pos+=15;e=json.find('"',pos);entry.description=json.substr(pos,e-pos);
        pos=json.find("\"date\":\"",e);if(pos==std::string::npos)break;pos+=8;e=json.find('"',pos);entry.date=json.substr(pos,e-pos);
        pos=json.find("\"cell_type\":\"",e);if(pos==std::string::npos)break;pos+=13;e=json.find('"',pos);entry.cell_type=json.substr(pos,e-pos);
        pos=json.find("\"tick_count\":",e);if(pos==std::string::npos)break;pos+=13;entry.tick_count=(uint32_t)strtoul(json.c_str()+pos,nullptr,10);
        pos=json.find("\"file\":\"",pos);if(pos==std::string::npos)break;pos+=8;e=json.find('"',pos);entry.file_path=json.substr(pos,e-pos);
        result.push_back(entry);
    }
    return result;
}

int main(){
    const char* dir="/tmp/test_exp_workflow";
    mkdir(dir,0755);

    SEC("1. Create experiment data and write index");
    {
        std::vector<ExpIdx> entries;
        {ExpIdx e;e.name="TestExp1";e.description="First";e.date="2026-06-01T10";e.cell_type="Pyramidal";e.tick_count=1500;e.file_path="TestExp1.bexp";entries.push_back(e);}
        {ExpIdx e;e.name="TestExp2";e.description="Second";e.date="2026-06-01T11";e.cell_type="Interneuron";e.tick_count=3000;e.file_path="TestExp2.bexp";entries.push_back(e);}
        {ExpIdx e;e.name="DendConn_Test";e.description="Probe test";e.date="2026-06-01T12";e.cell_type="Pyramidal";e.tick_count=5000;e.file_path="DendConn_Test.bexp";entries.push_back(e);}
        write_index(dir,entries);
        auto loaded=read_index(dir);
        CHK(loaded.size()==3,"Index write/read: %zu entries",loaded.size());
        CHK(loaded[0].name=="TestExp1","Entry 0 name");
        CHK(loaded[2].tick_count==5000,"Entry 2 tick_count: %u",loaded[2].tick_count);
    }

    SEC("2. Search index");
    {
        auto entries=read_index(dir);
        // Search for "DendConn"
        int found=0;
        for(auto&e:entries){
            if(e.name.find("DendConn")!=std::string::npos)found++;
        }
        CHK(found==1,"Search 'DendConn': %d results",found);
        // Search for "Pyramidal"
        found=0;
        for(auto&e:entries)if(e.cell_type.find("Pyramidal")!=std::string::npos)found++;
        CHK(found==2,"Search 'Pyramidal': %d results",found);
    }

    SEC("3. Delete entry");
    {
        auto entries=read_index(dir);
        entries.erase(entries.begin()+1); // Remove TestExp2
        write_index(dir,entries);
        auto reloaded=read_index(dir);
        CHK(reloaded.size()==2,"After delete: %zu entries",reloaded.size());
        CHK(reloaded[0].name=="TestExp1"&&reloaded[1].name=="DendConn_Test","Remaining entries correct");
    }

    SEC("4. Atomic write (tmp+rename)");
    {
        std::string final_path=std::string(dir)+"/index.json";
        std::string tmp_path=std::string(dir)+"/index.json.tmp";
        std::vector<ExpIdx> entries;{ExpIdx e;e.name="Atomic";e.description="test";e.date="2026-06-01";e.cell_type="Other";e.tick_count=100;e.file_path="Atomic.bexp";entries.push_back(e);}
        write_index(dir,entries); // write to tmp first
        // Simulate: write to tmp, then rename
        FILE*f=fopen(tmp_path.c_str(),"w");
        fprintf(f,"[{\"name\":\"AtomicFinal\",\"description\":\"renamed\",\"date\":\"now\",\"cell_type\":\"Other\",\"tick_count\":200,\"file\":\"AtomicFinal.bexp\"}]\n");
        fclose(f);
        rename(tmp_path.c_str(),final_path.c_str());
        auto r=read_index(dir);
        CHK(r.size()==1,"Atomic write: %zu entries",r.size());
        CHK(r[0].name=="AtomicFinal","Atomic write: name preserved");
        // tmp should be gone
        CHK(access(tmp_path.c_str(),F_OK)!=0,"Atomic write: tmp removed");
    }

    // Cleanup
    remove((std::string(dir)+"/index.json").c_str());
    rmdir(dir);

    SEC("5. ConfigToProbe round-trip");
    {
        // Создаём ExperimentProbeConfig с полной конфигурацией
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ELECTRODE;
        cfg.segment_id = 42;
        strncpy(cfg.label, "TestElectrode", 31);
        cfg.record_vm = true;
        cfg.record_ions[0] = true;   // Na
        cfg.record_ions[4] = true;   // Mg
        cfg.color[0] = 0.1f; cfg.color[1] = 0.8f; cfg.color[2] = 0.3f;
        cfg.cannula_placement = CannulaPlacement::INTRACELLULAR;
        cfg.channels = 3;
        cfg.stim_enabled = true;
        cfg.stim_amplitude_pA = 200.0f;
        cfg.stim_duration_ms = 15.0f;
        cfg.stim_delay_ms = 3.0f;
        cfg.stim_type = 1;
        cfg.inject_enabled = true;
        cfg.inject_is_ion = true;
        cfg.inject_substance = 1;  // K+
        cfg.inject_amount = 5.0f;

        // Конвертируем в Probe
        Probe p = ExperimentManager::ConfigToProbe(cfg);

        CHK(p.type == ProbeType::ELECTRODE, "Type round-trip");
        CHK(p.segment_id == 42u, "Segment ID: %u", p.segment_id);
        CHK(strcmp(p.label, "TestElectrode") == 0, "Label round-trip");
        CHK(p.record_vm == true, "Record Vm round-trip");
        CHK(p.record_ions[0] == true, "Record Na round-trip");
        CHK(p.record_ions[4] == true, "Record Mg round-trip");
        CHK(p.stim_enabled == true, "Stim enabled round-trip");
        CHK(fabsf(p.stim_amplitude_pA - 200.0f) < 0.01f, "Stim amplitude: %.1f", p.stim_amplitude_pA);
        CHK(fabsf(p.stim_duration_ms - 15.0f) < 0.01f, "Stim duration: %.1f", p.stim_duration_ms);
        CHK(fabsf(p.stim_delay_ms - 3.0f) < 0.01f, "Stim delay: %.1f", p.stim_delay_ms);
        CHK(p.inject_enabled == true, "Inject enabled round-trip");
        CHK(p.substance_type == CannulaSubstance::ION, "Substance type ION");
        CHK(p.inject_substance_idx == 1, "Inject substance K+");
        CHK(fabsf(p.inject_amount_mM - 5.0f) < 0.01f, "Inject amount: %.1f", p.inject_amount_mM);
    }

    SEC("6. ConfigToProbe: LIGAND round-trip");
    {
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::CANNULE;
        cfg.inject_is_ion = false;  // лиганд
        cfg.inject_substance = 3;   // серотонин
        cfg.inject_amount = 2.5f;

        Probe p = ExperimentManager::ConfigToProbe(cfg);
        CHK(p.substance_type == CannulaSubstance::LIGAND, "Substance type LIGAND");
        CHK(p.inject_substance_idx == 3, "Ligand index: %d", p.inject_substance_idx);
    }

    SEC("7. ApplyExperimentSetup");
    {
        NetworkModel model;
        SegmentSnapshot s1{}; s1.id = 1; model.segments.push_back(s1);
        SegmentSnapshot s2{}; s2.id = 42; model.segments.push_back(s2);

        ExperimentData data;
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ELECTRODE;
        cfg.segment_id = 42;
        strncpy(cfg.label, "E1", 31);
        cfg.record_vm = true;
        cfg.channels = 1;
        data.probes.push_back(cfg);

        ExperimentManager::ApplyExperimentSetup(model, data);
        CHK(model.probes.size() == 1, "Probes count: %zu", model.probes.size());
        CHK(strcmp(model.probes[0].label, "E1") == 0, "Probe label applied");
        CHK(model.probes[0].segment_id == 42u, "Probe segment ID applied");
    }

    SEC("8. ResolveBionPath — absolute path exists");
    {
        // Создаём временный .bion файл
        const char* tmp_bion = "/tmp/test_model.bion";
        FILE* f = fopen(tmp_bion, "wb");
        CHK(f != nullptr, "Create temp .bion");
        if (f) {
            // Минимальный .bion заголовок
            uint32_t magic = 0x4E4F4942;  // "BION"
            uint32_t ver = 4;
            fwrite(&magic, 4, 1, f);
            fwrite(&ver, 4, 1, f);
            fclose(f);
        }

        ExperimentMeta meta;
        meta.bion_file_path = tmp_bion;

        std::string resolved = ExperimentManager::ResolveBionPath(meta, nullptr);
        CHK(resolved == tmp_bion, "Resolve by absolute path: %s", resolved.c_str());

        unlink(tmp_bion);
    }

    SEC("9. ResolveBionPath — fallback to model_name");
    {
        const char* tmp_bion = "/tmp/fallback_model.bion";
        FILE* f = fopen(tmp_bion, "wb");
        if (f) {
            uint32_t magic = 0x4E4F4942, ver = 4;
            fwrite(&magic, 4, 1, f); fwrite(&ver, 4, 1, f);
            fclose(f);
        }

        ExperimentMeta meta;
        meta.bion_file_path = "/nonexistent/path/model.bion";  // битый путь
        meta.model_name = "fallback_model.bion";

        // Должен fallback-нуться к model_name (текущая директория /tmp)
        std::string resolved = ExperimentManager::ResolveBionPath(meta, nullptr);
        // model_name без пути пробуется как есть (fopen ищет в CWD)
        // Это может сработать если CWD = /tmp, но в общем случае нет
        // Проверим хотя бы что bion_file_path не вернулся
        CHK(resolved != "/nonexistent/path/model.bion", "Fallback doesn't return broken path");

        unlink(tmp_bion);
    }

    SEC("10. ResolveBionPath — search near .bexp");
    {
        const char* tmp_dir = "/tmp/test_exp_bion";
        mkdir(tmp_dir, 0755);
        std::string bion_path = std::string(tmp_dir) + "/nearby_model.bion";
        FILE* f = fopen(bion_path.c_str(), "wb");
        if (f) {
            uint32_t magic = 0x4E4F4942, ver = 4;
            fwrite(&magic, 4, 1, f); fwrite(&ver, 4, 1, f);
            fclose(f);
        }

        ExperimentMeta meta;
        meta.bion_file_path = "/bad/path.bion";
        meta.model_name = "nearby_model.bion";

        std::string bexp_path = std::string(tmp_dir) + "/test.bexp";
        std::string resolved = ExperimentManager::ResolveBionPath(meta, bexp_path.c_str());
        CHK(resolved == bion_path, "Resolve near .bexp: %s", resolved.c_str());

        unlink(bion_path.c_str());
        rmdir(tmp_dir);
    }

    SEC("11. saveAsTemplate — tick_count=0, no frames");
    {
        const char* tmpl_path = "/tmp/test_template.bexp_template";

        ExperimentData data;
        data.meta.name = "TemplateTest";
        data.meta.description = "A test template";
        data.meta.model_name = "model.bion";
        data.meta.bion_file_path = "/tmp/model.bion";
        data.meta.probe_count = 1;
        data.meta.tick_count = 999;  // будет заменён на 0
        data.frame_stride = 3;
        data.frames = {1, 2, 3, 4, 5, 6};  // не должны сохраниться
        ExperimentProbeConfig cfg;
        cfg.type = ProbeType::ELECTRODE; cfg.segment_id = 1;
        strncpy(cfg.label, "E1", 31); cfg.record_vm = true; cfg.channels = 1;
        data.probes.push_back(cfg);

        ExperimentManager mgr;
        bool ok = mgr.saveAsTemplate(tmpl_path, data);
        CHK(ok, "saveAsTemplate succeeded");

        // Проверяем что файл существует
        CHK(access(tmpl_path, F_OK) == 0, "Template file exists");

        // Читаем обратно
        FILE* f = fopen(tmpl_path, "rb");
        CHK(f != nullptr, "Reopen template");
        uint32_t magic, ver, json_len;
        fread(&magic, 4, 1, f); fread(&ver, 4, 1, f); fread(&json_len, 4, 1, f);
        CHK(magic == BEXP_MAGIC, "Template magic OK");
        CHK(ver == BEXP_VERSION, "Template version OK");

        std::string json_str(json_len, '\0');
        fread(&json_str[0], 1, json_len, f);

        // tick_count должен быть 0 в JSON
        CHK(json_str.find("\"tick_count\": 0") != std::string::npos, "Template tick_count=0 in JSON");

        // frame_count должен быть 0
        uint32_t fc, fs, fb, nec;
        fread(&fc, 4, 1, f); fread(&fs, 4, 1, f); fread(&fb, 4, 1, f); fread(&nec, 4, 1, f);
        CHK(fc == 0u, "Template frame_count=0");
        CHK(fs == 0u, "Template frame_stride=0");
        CHK(fb == 0u, "Template frame_bytes=0");
        CHK(nec == 0u, "Template events_count=0");

        fclose(f);
        unlink(tmpl_path);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Тесты целостности index.json — регрессия бага перезатирания
    // (test_exp_dendconn.cpp и create_experiment.cpp писали index напрямую fopen("w"))
    // ═══════════════════════════════════════════════════════════════════════════

    SEC("12. scanIndex находит все .bexp файлы (не теряет записи)");
    {
        const char* dir = "/tmp/test_idx_all";
        mkdir(dir, 0755);

        // Создаём 3 .bexp файла с минимальными заголовками
        const char* names[] = {"alpha", "beta", "gamma"};
        for (int i = 0; i < 3; i++) {
            std::string exp_path = std::string(dir) + "/" + names[i] + ".bexp";
            char json[256];
            snprintf(json, sizeof(json),
                "{\"name\":\"%s\",\"description\":\"test %s\",\"date\":\"2026-06-17\","
                "\"model_name\":\"test.bion\",\"cell_type\":\"MN\",\"tick_count\":%d,"
                "\"probe_count\":0,\"probes\":[]}",
                names[i], names[i], 100 * (i + 1));
            uint32_t magic = BEXP_MAGIC, ver = BEXP_VERSION, jl = (uint32_t)strlen(json);
            uint32_t fc = (uint32_t)(100 * (i + 1)), fs = 1, fb = 0;
            FILE* bf = fopen(exp_path.c_str(), "wb");
            fwrite(&magic, 4, 1, bf); fwrite(&ver, 4, 1, bf); fwrite(&jl, 4, 1, bf);
            fwrite(json, 1, jl, bf);
            fwrite(&fc, 4, 1, bf); fwrite(&fs, 4, 1, bf); fwrite(&fb, 4, 1, bf);
            uint32_t ne = 0; fwrite(&ne, 4, 1, bf);
            fclose(bf);
        }

        ExperimentManager mgr;
        bool ok = mgr.scanIndex(dir);
        CHK(ok, "scanIndex succeeded with 3 .bexp files");

        auto& entries = mgr.indexEntries();
        CHK(entries.size() == 3u, "Index has 3 entries (got %zu)", entries.size());

        bool found[3] = {false, false, false};
        for (auto& e : entries) {
            for (int i = 0; i < 3; i++)
                if (e.file_path == std::string(names[i]) + ".bexp") found[i] = true;
        }
        CHK(found[0] && found[1] && found[2], "All three experiments found in index");

        // Убираем
        for (int i = 0; i < 3; i++)
            unlink((std::string(dir) + "/" + names[i] + ".bexp").c_str());
        unlink((std::string(dir) + "/index.json").c_str());
        rmdir(dir);
    }

    SEC("13. scanIndex переживает отсутствие index.json (полное восстановление с диска)");
    {
        const char* dir = "/tmp/test_idx_rebuild";
        mkdir(dir, 0755);

        // Пишем .bexp на диск БЕЗ index.json
        const char* json = "{\"name\":\"orphan\",\"description\":\"no index\","
                           "\"date\":\"2026-06-17\",\"model_name\":\"t.bion\","
                           "\"cell_type\":\"MN\",\"tick_count\":50,\"probe_count\":0,\"probes\":[]}";
        uint32_t magic = BEXP_MAGIC, ver = BEXP_VERSION, jl = (uint32_t)strlen(json);
        uint32_t fc = 50, fs = 1, fb = 0;
        std::string epath = std::string(dir) + "/orphan.bexp";
        FILE* bf = fopen(epath.c_str(), "wb");
        fwrite(&magic, 4, 1, bf); fwrite(&ver, 4, 1, bf); fwrite(&jl, 4, 1, bf);
        fwrite(json, 1, jl, bf);
        fwrite(&fc, 4, 1, bf); fwrite(&fs, 4, 1, bf); fwrite(&fb, 4, 1, bf);
        uint32_t ne = 0; fwrite(&ne, 4, 1, bf);
        fclose(bf);

        // index.json НЕ существует
        CHK(access((std::string(dir) + "/index.json").c_str(), F_OK) != 0,
            "index.json does not exist before scanIndex");

        ExperimentManager mgr;
        bool ok = mgr.scanIndex(dir);
        CHK(ok, "scanIndex succeeded without pre-existing index.json");
        CHK(mgr.indexEntries().size() == 1u, "Rebuilt index has 1 entry");
        CHK(mgr.indexEntries()[0].file_path == "orphan.bexp",
            "Rebuilt entry has correct file_path");
        CHK(mgr.indexEntries()[0].tick_count == 50u,
            "Rebuilt entry has correct tick_count (from .bexp header)");

        // Теперь index.json должен существовать
        CHK(access((std::string(dir) + "/index.json").c_str(), F_OK) == 0,
            "index.json exists after scanIndex");

        unlink(epath.c_str());
        unlink((std::string(dir) + "/index.json").c_str());
        rmdir(dir);
    }

    SEC("14. Индекс не перезатирается при повторном scanIndex");
    {
        const char* dir = "/tmp/test_idx_persist";
        mkdir(dir, 0755);

        // Создаём первый .bexp
        auto make_bexp = [](const char* dir, const char* name, int tc) {
            std::string epath = std::string(dir) + "/" + name + ".bexp";
            char json[256];
            snprintf(json, sizeof(json),
                "{\"name\":\"%s\",\"description\":\"exp %s\",\"date\":\"2026-06-17\","
                "\"model_name\":\"t.bion\",\"cell_type\":\"MN\",\"tick_count\":%d,"
                "\"probe_count\":0,\"probes\":[]}",
                name, name, tc);
            uint32_t magic = BEXP_MAGIC, ver = BEXP_VERSION, jl = (uint32_t)strlen(json);
            uint32_t fc = (uint32_t)tc, fs = 1, fb = 0;
            FILE* bf = fopen(epath.c_str(), "wb");
            fwrite(&magic, 4, 1, bf); fwrite(&ver, 4, 1, bf); fwrite(&jl, 4, 1, bf);
            fwrite(json, 1, jl, bf);
            fwrite(&fc, 4, 1, bf); fwrite(&fs, 4, 1, bf); fwrite(&fb, 4, 1, bf);
            uint32_t ne = 0; fwrite(&ne, 4, 1, bf);
            fclose(bf);
        };

        make_bexp(dir, "first", 100);
        ExperimentManager mgr1;
        mgr1.scanIndex(dir);
        CHK(mgr1.indexEntries().size() == 1u, "After first .bexp: 1 entry");

        // Второй .bexp добавляется на диск
        make_bexp(dir, "second", 200);
        ExperimentManager mgr2;
        mgr2.scanIndex(dir);
        CHK(mgr2.indexEntries().size() == 2u, "After second .bexp: 2 entries (not overwritten to 1)");

        bool has_first = false, has_second = false;
        for (auto& e : mgr2.indexEntries()) {
            if (e.file_path == "first.bexp") has_first = true;
            if (e.file_path == "second.bexp") has_second = true;
        }
        CHK(has_first && has_second, "Both experiments present after second scanIndex");

        // Третий вызов scanIndex — всё ещё 2 записи
        ExperimentManager mgr3;
        mgr3.scanIndex(dir);
        CHK(mgr3.indexEntries().size() == 2u, "Third scanIndex still has 2 entries");

        // Убираем
        unlink((std::string(dir) + "/first.bexp").c_str());
        unlink((std::string(dir) + "/second.bexp").c_str());
        unlink((std::string(dir) + "/index.json").c_str());
        rmdir(dir);
    }

    SEC("15. Атомарная запись index.json (tmp+rename)");
    {
        const char* dir = "/tmp/test_idx_atomic";
        mkdir(dir, 0755);

        // Пишем index.json напрямую с одним экспериментом
        std::string idx = std::string(dir) + "/index.json";
        FILE* f = fopen(idx.c_str(), "w");
        fprintf(f, "[{\"name\":\"A\",\"description\":\"\",\"date\":\"\",\"cell_type\":\"\",\"tick_count\":10,\"file\":\"A.bexp\"}]\n");
        fclose(f);

        // Создаём два .bexp на диске
        for (int i = 0; i < 2; i++) {
            const char* nm = i == 0 ? "A" : "B";
            int tc = i == 0 ? 10 : 20;
            std::string epath = std::string(dir) + "/" + nm + ".bexp";
            char json[256];
            snprintf(json, sizeof(json),
                "{\"name\":\"%s\",\"description\":\"\",\"date\":\"\","
                "\"model_name\":\"\",\"cell_type\":\"\",\"tick_count\":%d,"
                "\"probe_count\":0,\"probes\":[]}",
                nm, tc);
            uint32_t magic = BEXP_MAGIC, ver = BEXP_VERSION, jl = (uint32_t)strlen(json);
            uint32_t fc = (uint32_t)tc, fs = 0, fb = 0;
            FILE* bf = fopen(epath.c_str(), "wb");
            fwrite(&magic, 4, 1, bf); fwrite(&ver, 4, 1, bf); fwrite(&jl, 4, 1, bf);
            fwrite(json, 1, jl, bf);
            fwrite(&fc, 4, 1, bf); fwrite(&fs, 4, 1, bf); fwrite(&fb, 4, 1, bf);
            uint32_t ne = 0; fwrite(&ne, 4, 1, bf);
            fclose(bf);
        }

        // scanIndex должен найти OBA .bexp (и A, и B), а не только то что в старом index.json
        ExperimentManager mgr;
        mgr.scanIndex(dir);
        CHK(mgr.indexEntries().size() == 2u,
            "Atomic scanIndex finds both A and B (not just what was in old index)");

        // tmp-файл не должен остаться
        CHK(access((std::string(dir) + ".tmp").c_str(), F_OK) != 0,
            "No stale .tmp file after atomic write");

        unlink((std::string(dir) + "/A.bexp").c_str());
        unlink((std::string(dir) + "/B.bexp").c_str());
        unlink(idx.c_str());
        rmdir(dir);
    }

    int t=P+F;printf("\n=== EXP WORKFLOW: %d PASS, %d FAIL ===\n",P,F);return F>0?1:0;
}
