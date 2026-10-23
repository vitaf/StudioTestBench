// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

// tadpole_experiment.cpp — АВТОНОМНЫЙ ЭКСПЕРИМЕНТАЛЬНЫЙ ХАРНЕСС
// Запуск: ./tadpole_experiment [--ticks N] [--output file.csv] [--kick pA] [--atp val]
// Без GUI. Генерация → симуляция → логирование хвоста и CPG-диагностики.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <vector>
#include <map>

#include "../BioNeuronStudio/src/data_model.h"
#include "../BioNeuronStudio/src/serialization.h"
#include "../BioNeuronStudio/src/simulation_bridge.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_types.h"
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_develop.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ── Хвост из MN-активации ──────────────────────────────────────────────────
struct TailMetrics {
    float angle, tip_x, tip_z, amplitude, left_norm, right_norm;
};

static TailMetrics computeTail(const std::vector<SegmentSnapshot>& segs) {
    float left_act = 0, right_act = 0;
    int left_n = 0, right_n = 0;
    for (const auto& s : segs) {
        if (s.type != SegmentType::SOMA) continue;
        if (s.position.y < 10.0f || s.position.y > 25.0f) continue;
        float spike = (s.V_m > -20.0f) ? 1.0f : 0.0f;
        if (s.position.x < 0.0f) { left_act += spike; left_n++; }
        else                     { right_act += spike; right_n++; }
    }
    float left_norm  = left_n  > 0 ? left_act / (float)left_n  : 0.0f;
    float right_norm = right_n > 0 ? right_act / (float)right_n : 0.0f;
    float angle = (right_norm - left_norm) * 0.3f;
    return {angle, 200.0f * sinf(angle), -200.0f * cosf(angle),
            fabsf(right_norm - left_norm), left_norm, right_norm};
}

struct FoodParticle { float x, y, z; };

struct ExpArgs {
    int   total_ticks   = 500;
    const char* output  = "/tmp/tadpole_exp.csv";
    float kick_amp      = 250.0f;
    float initial_atp   = 0.00015f;
    float hunger_max_pA = 40.0f;
};

static void parseArgs(int argc, char** argv, ExpArgs& a) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ticks") && i+1 < argc) a.total_ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--output") && i+1 < argc) a.output = argv[++i];
        else if (!strcmp(argv[i], "--kick") && i+1 < argc) a.kick_amp = atof(argv[++i]);
        else if (!strcmp(argv[i], "--atp") && i+1 < argc) a.initial_atp = atof(argv[++i]);
        else if (!strcmp(argv[i], "--hunger") && i+1 < argc) a.hunger_max_pA = atof(argv[++i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
int main(int argc, char** argv) {
    ExpArgs args;
    parseArgs(argc, argv, args);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   TADPOLE EXPERIMENT HARNESS v2         ║\n");
    printf("║   ticks=%d  kick=%.0fpA  atp=%.5f       ║\n", args.total_ticks, args.kick_amp, args.initial_atp);
    printf("╚══════════════════════════════════════════╝\n\n");

    // ══ 1. Генерация ═══════════════════════════════════════════════════════════
    printf("[EXP] Генерация коннектома...\n");
    TadpoleGenerationParams params = default_tadpole_params(TadpoleStage::LATE_TAILBUD);
    params.rb_per_hemisegment  = 3;
    params.din_per_hemisegment = 2;
    params.cin_per_hemisegment = 2;
    params.mn_per_hemisegment  = 2;
    params.ain_per_hemisegment = 1;
    params.enable_hunger_sensor = true;
    params.enable_light_sensor  = false;
    params.enable_stdp = params.enable_consolidation = false;

    TadpoleDevelop dev(params);
    NetworkModel model = dev.generate_connectome();
    printf("[EXP] Сегментов:%zu синапсов:%zu нейронов:%zu\n",
           model.segments.size(), model.synapses.size(), model.neurons.size());

    // ══ 2. Классификация нейронов (по позиции) ════════════════════════════════
    std::vector<uint32_t> left_din, right_din, left_cin, right_cin;
    std::vector<uint32_t> mn_ids, hypo_ids, rb_ids;
    std::map<int,int> dv_hist;

    for (const auto& seg : model.segments) {
        if (seg.type != SegmentType::SOMA) continue;
        float y = seg.position.y, x = seg.position.x, z = seg.position.z;
        dv_hist[(int)(y/10.0f)*10]++;

        if (y > 60.0f && y < 80.0f) {
            (x < 0.0f ? left_din : right_din).push_back(seg.id);
        } else if (y > 40.0f && y < 55.0f) {
            (x < 0.0f ? left_cin : right_cin).push_back(seg.id);
        } else if (y > 10.0f && y < 25.0f) {
            mn_ids.push_back(seg.id);
        } else if (y > 40.0f && y < 60.0f && z > 100.0f) {
            hypo_ids.push_back(seg.id);
        } else if (y > 85.0f && y < 95.0f) {
            rb_ids.push_back(seg.id);
        }
    }

    printf("[EXP] DV-гистограмма:");
    for (auto& [bin, cnt] : dv_hist) printf(" %d:%d", bin, cnt);
    printf("\n[EXP] dIN L:%zu R:%zu | cIN L:%zu R:%zu | MN:%zu | HYPO:%zu | RB:%zu\n",
           left_din.size(), right_din.size(), left_cin.size(), right_cin.size(),
           mn_ids.size(), hypo_ids.size(), rb_ids.size());

    // ══ 3. Начальный ATP + сохранение ═════════════════════════════════════════
    int soma_n = 0;
    for (auto& seg : model.segments)
        if (seg.type == SegmentType::SOMA) { seg.ATP_level = args.initial_atp; soma_n++; }
    printf("[EXP] ATP=%.5f на %d сомах\n", args.initial_atp, soma_n);
    // Диагностика ионов модели ДО сохранения
    { int n=0; for (auto& s : model.segments) { if(s.type!=SegmentType::SOMA) continue; if(++n>3) break;
        printf("[EXP] Модель seg=%u Na=%.4f K=%.4f Ca=%.6f Cl=%.4f\n",
               s.id, s.intra_ions[0], s.intra_ions[1], s.intra_ions[2], s.intra_ions[3]); } }
    SaveNetwork(model, "/tmp/tadpole_exp.bion");

    // ══ 4. SimulationBridge ════════════════════════════════════════════════════
    SimulationBridge sim;
    sim.setBackend(SimulationBridge::Backend::CPU);
    if (!sim.loadNetwork("/tmp/tadpole_exp.bion")) { printf("[EXP] FAIL: load\n"); return 1; }

    // ══ 5. Старт + kick-start ══════════════════════════════════════════════════
    sim.setSpeedMultiplier(10.0f);
    sim.start();
    usleep(200000);  // 200ms на инициализацию

    // ── Диагностика начального состояния ─────────────────────────────────
    {
        std::vector<SegmentSnapshot> ss; sim.getSegmentsSnapshot(ss);
        printf("[EXP] Снапшот: %zu сегментов всего\n", ss.size());
        int shown=0, soma_n=0;
        for (auto& s : ss) {
            if (s.type == SegmentType::SOMA) soma_n++;
            if (s.position.y > 60 && s.position.y < 80 && s.type == SegmentType::SOMA) {
                if (++shown > 3) break;
                printf("  dIN seg=%u type=%d Vm=%.1f ATP=%.5f Na=%.4f K=%.4f Ca=%.6f Cl=%.4f\n",
                       s.id, (int)s.type, s.V_m, s.ATP_level,
                       s.intra_ions[0], s.intra_ions[1], s.intra_ions[2], s.intra_ions[3]);
            }
        }
        printf("[EXP] Сом в снапшоте: %d, dIN показано: %d\n", soma_n, shown);
    }

    // Непрерывный alternating kick-start: 60ms цикл (~16 Hz плавательный ритм)
    printf("[EXP] Continuous alternating kick-start: cycle=60ms, %.0fpA×15ms (adaptive)\n", args.kick_amp);
    float last_kick_side = 1.0f;
    float next_kick_time = 0.0f;

    // ══ 6. Главный цикл ════════════════════════════════════════════════════════
    FILE* flog = fopen(args.output, "w");
    fprintf(flog, "tick,Vm_din_L,Vm_din_R,Vm_cin_L,Vm_cin_R,Vm_mn_L,Vm_mn_R,"
            "tail_angle,tail_x,tail_z,tail_amp,gly_dL,gly_dR,atp,food_n,body_x,body_z,heading,"
            "ion_Na,ion_K,ion_Ca,ion_Cl\n");

    std::vector<FoodParticle> food = {{50,-10,-100}, {-30,-10,-80}, {20,-10,-150}};
    float bx=0, bz=0, bh=0, max_amp=0;
    int moved=0, hunger_cd=0, logged=0;
    uint64_t last_tick=0;

    while (logged < args.total_ticks / 10) {  // логируем ~каждые 10 тиков
        usleep(2000);  // 2ms между опросами
        uint64_t t = sim.getCurrentTick();
        if (t == last_tick) continue;
        last_tick = t;

        // Голод
        if (hunger_cd <= 0 && !hypo_ids.empty()) {
            float atp = args.initial_atp;
            { std::vector<SegmentSnapshot> ss; sim.getSegmentsSnapshot(ss);
              float s=0; int n=0; for(auto& x:ss) if(x.type==SegmentType::SOMA){s+=x.ATP_level;n++;}
              if(n) atp=s/n; }
            float an = atp * 1000.0f; if(an>1)an=1;
            if (an < 0.5f)
                for (auto id : hypo_ids)
                    sim.addCurrentStimulus(id, (1.0f-an)*args.hunger_max_pA, 50.0f, 0.0f);
            hunger_cd = 20;
        }
        hunger_cd--;

        // ── Непрерывный alternating kick-start (адаптивный, быстрый) ──
        {
            float sim_time_ms = (float)t * 0.1f;
            if (sim_time_ms >= next_kick_time) {
                last_kick_side = -last_kick_side;
                bool kick_left = (last_kick_side < 0);
                auto& target_din = kick_left ? left_din : right_din;
                float amp = args.kick_amp;
                for (auto id : target_din) {
                    const SegmentSnapshot* f = nullptr;
                    for (auto& s : model.segments) if (s.id == id) { f = &s; break; }
                    if (f && (int)(-f->position.z/150.0f+0.5f) <= 1)
                        sim.addCurrentStimulus(id, amp, 15.0f, 0.0f);
                }
                next_kick_time += 60.0f;  // 60ms half-cycle = 120ms полный цикл (~8 Hz) — время для PIR
            }
        }

        // ── Фоновый тонический драйв на dIN (оба стороны) ─────────────
        // Поддерживает базовую возбудимость, предотвращает падение до 0mV
        static int bg_drive_cd = 0;
        if (--bg_drive_cd <= 0) {
            for (auto id : left_din)
                sim.addCurrentStimulus(id, 40.0f, 3.0f, 0.0f);
            for (auto id : right_din)
                sim.addCurrentStimulus(id, 40.0f, 3.0f, 0.0f);
            bg_drive_cd = 50;  // каждые 50 тиков (~5ms реального времени)
        }

        // Еда
        for (auto it=food.begin(); it!=food.end(); ) {
            if (sqrtf((bx-it->x)*(bx-it->x)+(bz-it->z)*(bz-it->z)) < 25.0f) {
                printf("[EXP] t=%lu: ЕДА съедена (%.0f,%.0f)\n", (unsigned long)t, it->x, it->z);
                it = food.erase(it);
            } else ++it;
        }

        // Лог
        {
            std::vector<SegmentSnapshot> segs; sim.getSegmentsSnapshot(segs);
            if (segs.empty()) continue;
            float dL=0,dR=0,cL=0,cR=0,mL=0,mR=0,gL=0,gR=0,at=0;
            int ndL=0,ndR=0,ncL=0,ncR=0,nmL=0,nmR=0,ngL=0,ngR=0,na=0;
            for (auto& s : segs) {
                if (s.type != SegmentType::SOMA) continue;
                at += s.ATP_level; na++;
                float y=s.position.y, x=s.position.x;
                if (y>60&&y<80) {
                    if (x<0) { dL+=s.V_m; ndL++; gL+=s.neurotransmitters[7]; ngL++; }
                    else     { dR+=s.V_m; ndR++; gR+=s.neurotransmitters[7]; ngR++; }
                } else if (y>40&&y<55) { if(x<0){cL+=s.V_m;ncL++;}else{cR+=s.V_m;ncR++;} }
                else if (y>10&&y<25)   { if(x<0){mL+=s.V_m;nmL++;}else{mR+=s.V_m;nmR++;} }
            }
            if(ndL)dL/=ndL; if(ndR)dR/=ndR; if(ncL)cL/=ncL; if(ncR)cR/=ncR;
            if(nmL)mL/=nmL; if(nmR)mR/=nmR; if(ngL)gL/=ngL; if(ngR)gR/=ngR;
            auto tail = computeTail(segs);
            if (tail.amplitude > max_amp) max_amp = tail.amplitude;
            if (tail.amplitude > 0.01f) moved++;
            // Диагностика ионов: левый и правый dIN раздельно
            float diag_na=0, diag_k=0, diag_ca=0, diag_cl=0;
            float diagR_na=0, diagR_k=0, diagR_ca=0, diagR_cl=0, diagR_vm=0;
            for (auto& s : segs) {
                if (s.type==SegmentType::SOMA && s.position.y>60&&s.position.y<80) {
                    if (s.position.x < 0) {
                        diag_na=s.intra_ions[0]; diag_k=s.intra_ions[1];
                        diag_ca=s.intra_ions[2]; diag_cl=s.intra_ions[3];
                    } else {
                        diagR_na=s.intra_ions[0]; diagR_k=s.intra_ions[1];
                        diagR_ca=s.intra_ions[2]; diagR_cl=s.intra_ions[3]; diagR_vm=s.V_m;
                    }
                }
            }

            float sp = (tail.left_norm+tail.right_norm)*0.5f;
            bx += sinf(bh)*sp*0.5f; bz += cosf(bh)*sp*0.5f; bh += tail.angle*0.03f;

            fprintf(flog, "%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.4f,%.1f,%.1f,%.3f,%.4f,%.4f,%.5f,%zu,%.1f,%.1f,%.3f,%.4f,%.4f,%.4f,%.4f\n",
                    (unsigned long)t, dL,dR,cL,cR,mL,mR, tail.angle,tail.tip_x,tail.tip_z,tail.amplitude,
                    gL,gR, na?at/na:0.0f, food.size(), bx,bz,bh, diag_na,diag_k,diag_ca,diag_cl);
            fflush(flog);
            if (++logged % 20 == 0)
                printf("[EXP] t=%-5lu dIN(L:%5.1f R:%5.1f) cIN(L:%5.1f R:%5.1f) tail=%.3f |L: K=%.2f Ca=%.4f |R: K=%.2f Ca=%.4f| food=%zu\n",
                       (unsigned long)t, dL,dR, cL,cR, tail.angle,
                       diag_k, diag_ca, diagR_k, diagR_ca, food.size());
        }
    }

    fclose(flog); sim.stop();
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   DONE: max_amp=%.3frad  moved=%d  food=%zu/3  ║\n", max_amp, moved, 3-food.size());
    printf("║   Output: %s║\n", args.output);
    printf("╚══════════════════════════════════════════╝\n");
    return (max_amp > 0.01f) ? 0 : 1;
}
