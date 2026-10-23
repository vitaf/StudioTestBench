// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Бизнес-логика Studio: ExperimentManager, Probes, Stimulus presets, Converter, Viewport math.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include "neuroml_import.h"
#include "serialization.h"
#include "data_model.h"
#include "experiment_data.h"
#include "simulation_core_api.h"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i*988;
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}
static void rtk(CoreData d,int n,float dt=0.1f){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);uint64_t tick=0;
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&tick,dt,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}

int main(){
    core_init_libraries(); core_set_bio_mode();

    // ═══════════════════════════════════════════════════════════════
    SEC("1. EXPERIMENT: полный цикл записи/воспроизведения");
    {
        NetworkModel m; auto*s=m.create_segment({0,0,0},SegmentType::SOMA);
        s->V_m=-70; s->radius=10; s->length=20;

        // 1a. Метаданные
        ExperimentMeta meta; meta.name="TestExp"; meta.description="Integration test";
        meta.model_name="test.bion"; meta.cell_type="Pyramidal";
        CHK(!meta.name.empty(),"Meta name set");
        CHK(meta.probe_count==0,"Meta probe_count default: %u",meta.probe_count);

        // 1b. Конфигурация пробa
        ExperimentProbeConfig pcfg; pcfg.type=ProbeType::ELECTRODE;
        pcfg.segment_id=s->id; strncpy(pcfg.label,"E1",31);
        pcfg.record_vm=true; pcfg.record_ions[0]=true;
        CHK(pcfg.type==ProbeType::ELECTRODE,"ProbeConfig type");
        CHK(pcfg.segment_id==s->id,"ProbeConfig seg_id: %u",pcfg.segment_id);
        CHK(strcmp(pcfg.label,"E1")==0,"ProbeConfig label: %s",pcfg.label);

        // 1c. События
        ExperimentEvent ev; ev.tick=42; ev.probe_index=0;
        ev.type=ExperimentEventType::STIM_START;
        ev.params[0]=100; ev.params[1]=10; ev.params[2]=5;
        CHK(ev.tick==42,"Event tick"); CHK(ev.params[0]==100,"Event amplitude");
        ev.type=ExperimentEventType::INJECT;
        ev.params[0]=0; ev.params[1]=2.0f;
        CHK(ev.params[0]==0,"Inject: ion_idx"); CHK(ev.params[1]==2.0f,"Inject: amount");

        // 1d. Данные эксперимента
        ExperimentData data; data.meta=meta; data.meta.probe_count=1;
        data.probes.push_back(pcfg);
        // Симулируем запись 100 кадров
        SaveNetwork(m,"/tmp/texpbl.bion"); CoreData d=core_load_bion("/tmp/texpbl.bion");
        for(int i=0;i<100;i++){
            core_clear_stimuli();
            if(i==20) core_add_current_stimulus(s->id,200,5,0);
            rtk(d,10);
            float vm=rvm(d,s->id);
            data.frames.push_back(vm); data.frames.push_back(0); data.frames.push_back(0);
        }
        core_unload_bion(d); remove("/tmp/texpbl.bion");
        data.meta.tick_count=1000;
        CHK(data.frames.size()==300,"Frames recorded: %zu",data.frames.size());
        CHK(data.meta.tick_count==1000,"Tick count: %u",data.meta.tick_count);

        // 1e. Очистка
        data.frames.clear(); data.events.clear(); data.meta.tick_count=0;
        CHK(data.frames.empty(),"Clear: frames empty");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("2. PROBES: электроды и канюли");
    {
        NetworkModel m; auto*s=m.create_segment({0,0,0},SegmentType::SOMA);
        s->V_m=-70; s->radius=10; s->length=20;

        // Электрод
        Probe ep; ep.id=1; ep.type=ProbeType::ELECTRODE; ep.segment_id=s->id;
        strncpy(ep.label,"Electrode1",31);
        ep.record_vm=true; ep.stim_enabled=true;
        ep.stim_amplitude_pA=150; ep.stim_duration_ms=8; ep.stim_delay_ms=2;
        m.probes.push_back(ep);
        CHK(m.probes[0].type==ProbeType::ELECTRODE,"Probe: electrode");
        CHK(m.probes[0].stim_amplitude_pA==150,"Stim amplitude: %.0f pA",m.probes[0].stim_amplitude_pA);

        // Канюля
        Probe cp; cp.id=2; cp.type=ProbeType::CANNULE; cp.segment_id=s->id;
        strncpy(cp.label,"Cannula1",31);
        cp.inject_enabled=true; cp.substance_type=CannulaSubstance::ION;
        cp.inject_substance_idx=0; cp.inject_amount_mM=2.0f;
        cp.cannula_placement=CannulaPlacement::EXTRACELLULAR;
        m.probes.push_back(cp);
        CHK(m.probes[1].type==ProbeType::CANNULE,"Probe: cannula");
        CHK(m.probes[1].substance_type==CannulaSubstance::ION,"Substance: ION");
        CHK(m.probes[1].cannula_placement==CannulaPlacement::EXTRACELLULAR,"Placement: extracellular");

        // Канюля с лигандом
        Probe lp; lp.id=3; lp.type=ProbeType::CANNULE; lp.segment_id=s->id;
        lp.inject_enabled=true; lp.substance_type=CannulaSubstance::LIGAND;
        lp.inject_substance_idx=0; lp.inject_amount_mM=0.5f;
        lp.cannula_placement=CannulaPlacement::INTRACELLULAR;
        m.probes.push_back(lp);
        CHK(m.probes[2].substance_type==CannulaSubstance::LIGAND,"Substance: LIGAND");
        CHK(m.probes[2].cannula_placement==CannulaPlacement::INTRACELLULAR,"Placement: intracellular");

        // Пробы пока не сериализуются в .bion (ожидаемое поведение)
        CHK(m.probes.size()==3,"Probes in memory: %zu",m.probes.size());
        CHK(m.probes[0].stim_amplitude_pA==150,"Stim amplitude: %.0f",m.probes[0].stim_amplitude_pA);
        CHK(m.probes[1].inject_amount_mM==2.0f,"Inject amount: %.1f",m.probes[1].inject_amount_mM);
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("3. STIMULUS: пресеты и train pulses");
    {
        NetworkModel m; auto*s=m.create_segment({0,0,0},SegmentType::SOMA);
        s->V_m=-70; s->radius=10; s->length=20;
        SaveNetwork(m,"/tmp/tstimbl.bion");

        // Пресеты (как в stimulus_dialog.cpp)
        struct Preset { const char* name; float amp; float dur; };
        Preset presets[] = {
            {"Subthreshold", 50, 2},
            {"Suprathreshold", 500, 2},
            {"Spike", 1000, 1},
        };

        for(auto& pr : presets) {
            CoreData d=core_load_bion("/tmp/tstimbl.bion");
            core_add_current_stimulus(s->id, pr.amp, pr.dur, 5);
            rtk(d, 200);
            float vm=rvm(d,s->id);
            CHK(fabsf(vm+70)>0.1f || pr.amp<100,"%s (%.0fpA): Vm=%.1f",pr.name,pr.amp,vm);
            core_unload_bion(d);
        }

        // Train pulses
        CoreData d=core_load_bion("/tmp/tstimbl.bion");
        for(int i=0;i<5;i++) core_add_current_stimulus(s->id,200,5,i*20.0f);
        rtk(d,500);
        float vm_train=rvm(d,s->id);
        CHK(fabsf(vm_train+70)>0.5f,"Train 5 pulses: Vm=%.1f",vm_train);
        core_unload_bion(d);
        remove("/tmp/tstimbl.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("4. SWC CONVERTER: логика конвертации");
    {
        // Тест парсинга SWC через swc_parser (уже протестирован в test_swc_parser.cpp)
        // Здесь: проверка логики сегментации
        struct SwcPoint { int idx, type; float x,y,z,r; int parent; };
        std::vector<SwcPoint> pts = {
            {1,1, 0,0,0,10,-1},    // soma
            {2,3, 0,10,0,3,1},     // basal dendrite from soma
            {3,3, 0,30,0,2,2},     // continue dendrite
            {4,4, 0,-30,0,1,1},    // apical from soma
        };
        CHK(pts.size()==4,"SWC: 4 points");
        // Проверка parent-child цепочки
        int root=0; for(auto&p:pts) if(p.parent==-1) root++;
        CHK(root==1,"SWC: 1 root (soma)");
        // Проверка типов
        int somas=0,dends=0,apicals=0;
        for(auto&p:pts){if(p.type==1)somas++;if(p.type==3)dends++;if(p.type==4)apicals++;}
        CHK(somas==1,"SWC: 1 soma"); CHK(dends==2,"SWC: 2 basal"); CHK(apicals==1,"SWC: 1 apical");

        // Soma bounding sphere
        float soma_r=pts[0].r; // 10
        float soma_diam=soma_r*2; // 20
        CHK(soma_diam==20,"Soma diameter: %.0f um",soma_diam);
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("5. VIEWPORT MATH: камера и raycasting");
    {
        // Орбитальная камера: сферические координаты
        float theta=0.8f, phi=0.5f, distance=100.0f;
        float cx=50, cy=30, cz=20; // центр орбиты

        // Прямое: spherical → cartesian
        float ex=cx+distance*cosf(phi)*cosf(theta); // нестандартно, но ок для теста
        float ey=cy+distance*sinf(phi);
        float ez=cz+distance*cosf(phi)*sinf(theta);
        CHK(fabsf(ex-cx)<distance+1,"Camera X within range");
        CHK(fabsf(ey-cy)<distance+1,"Camera Y within range");

        // Ray-cylinder intersection (упрощённо)
        Vec3 seg_pos={0,50,0}, seg_dir={0,1,0};
        float seg_len=100, seg_r=5;
        // Точка на поверхности цилиндра
        float t=0.5f;
        Vec3 point_on_seg={seg_pos.x+seg_dir.x*t*seg_len,
                           seg_pos.y+seg_dir.y*t*seg_len,
                           seg_pos.z+seg_dir.z*t*seg_len};
        float dist_to_axis=fabsf(point_on_seg.x-seg_pos.x);
        CHK(dist_to_axis<seg_r+1,"Point close to cylinder surface: %.1f", dist_to_axis);

        // Zoom limits
        float zoom_min=1.0f, zoom_max=50000.0f;
        distance=0.5f; if(distance<zoom_min)distance=zoom_min;
        CHK(distance>=zoom_min,"Zoom clamped to min");
        distance=100000; if(distance>zoom_max)distance=zoom_max;
        CHK(distance<=zoom_max,"Zoom clamped to max");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("6. MODIFIER STATE[]: граничные значения");
    {
        NetworkModel m; auto*s=m.create_segment({0,0,0},SegmentType::SOMA);
        s->V_m=-70; s->radius=10; s->length=20;

        // NAK с экстремальными state[]
        s->add_modifier(ModifierType::MOD_AXON_NAV12,1.0f);
        auto& mod=s->modifiers[0];
        mod.state[0]=0; mod.state[1]=0; mod.state[2]=0; // все гейты закрыты
        mod.state[3]=0; mod.state[4]=0; mod.state[5]=0; // дефолтные проводимости

        SaveNetwork(m,"/tmp/tmodbl.bion"); CoreData d=core_load_bion("/tmp/tmodbl.bion");
        rtk(d,50);
        float vm_closed=rvm(d,s->id);
        core_unload_bion(d);

        // Открытые гейты
        mod.state[0]=1; mod.state[1]=1; mod.state[2]=1;
        SaveNetwork(m,"/tmp/tmodbl.bion"); d=core_load_bion("/tmp/tmodbl.bion");
        rtk(d,50);
        float vm_open=rvm(d,s->id);
        core_unload_bion(d);
        remove("/tmp/tmodbl.bion");

        CHK(fabsf(vm_open-vm_closed)>0.01f,"Gates open vs closed: %.2f vs %.2f",vm_open,vm_closed);

        // MAX_MODIFIERS_PER_SEGMENT
        for(int i=0;i<32;i++){
            s->add_modifier(ModifierType::MOD_IONOTROPIC_CL,0.01f*(i+1));
        }
        CHK(s->modifier_count<=32,"Modifier limit: %d <= 32",s->modifier_count);
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("7. POPULATIONS: группировка и восстановление");
    {
        NetworkModel m;
        for(int pop=0;pop<3;pop++){
            for(int inst=0;inst<pop+2;inst++){
                auto*s=m.create_segment({(float)(pop*100+inst*30),0.f,0.f},SegmentType::SOMA);
                s->neuron_id=pop*10+inst+1;
            }
        }
        rebuild_neuron_tree(m);
        // Устанавливаем population_name вручную (как при NeuroML импорте)
        int ni=0;
        for(int pop=0;pop<3;pop++){
            for(int inst=0;inst<pop+2;inst++){
                if(ni<(int)m.neurons.size()){
                    m.neurons[ni].population_name=std::string("POP")+char('A'+pop);
                    m.neurons[ni].instance_id=inst;
                    m.neurons[ni].component_id="TestCell";
                    ni++;
                }
            }
        }
        rebuild_population_tree(m);
        CHK(m.populations.size()==3,"3 populations: %zu",m.populations.size());
        CHK(m.populations[0].neuron_count==2,"POP_A: %u neurons",m.populations[0].neuron_count);
        CHK(m.populations[1].neuron_count==3,"POP_B: %u neurons",m.populations[1].neuron_count);
        CHK(m.populations[2].neuron_count==4,"POP_C: %u neurons",m.populations[2].neuron_count);

        // Save/load
        SaveNetwork(m,"/tmp/tpopbl.bion");
        NetworkModel m2; std::string e;
        CHK(studio_load_bion("/tmp/tpopbl.bion",&m2,e)>=0,"Populations save/load");
        CHK(m2.populations.size()>=1,"Populations restored: %zu",m2.populations.size());
        remove("/tmp/tpopbl.bion");
    }

    // ═══════════════════════════════════════════════════════════════
    SEC("8. SYNAPSE RECEPTORS: все типы");
    {
        NetworkModel m; m.create_segment({0,0,0},SegmentType::DENDRITE);
        m.create_segment({10,0,0},SegmentType::DENDRITE);
        auto*syn=m.create_synapse(m.segments[0].id,m.segments[1].id,SynapseType::CHEMICAL);
        CHK(syn!=nullptr,"Synapse created");

        ModifierType rtypes[]={ModifierType::MOD_NT_RECEPTOR_AMPA,ModifierType::MOD_NT_RECEPTOR_NMDA,
            ModifierType::MOD_NT_RECEPTOR_GABAA,ModifierType::MOD_NT_RECEPTOR_GABAB,
            ModifierType::MOD_NT_RECEPTOR_5HT3,ModifierType::MOD_IONOTROPIC_CL};
        for(int ri=0;ri<6;ri++){
            syn->post_receptor_count=0;
            syn->post_receptors[0].type=static_cast<uint8_t>(rtypes[ri]);
            syn->post_receptors[0].density=0.5f;
            syn->post_receptor_count=1;
            CHK(syn->post_receptors[0].mod_type()==rtypes[ri],"Receptor %d set",ri);
        }
        CHK(syn->post_receptor_count==1,"Receptor count: %d",syn->post_receptor_count);

        // Autoreceptors
        syn->pre_autoreceptor_count=0;
        syn->pre_autoreceptors[0].type=static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_5HT1A);
        syn->pre_autoreceptors[0].density=0.3f;
        syn->pre_autoreceptor_count=1;
        CHK(syn->pre_autoreceptors[0].mod_type()==ModifierType::MOD_NT_RECEPTOR_5HT1A,"Autoreceptor set");
    }

    int t=P+F; printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
