// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Симуляционные сценарии: спайки, multi-neuron interaction, V_m recording.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "neuroml_import.h"
#include "serialization.h"
#include "data_model.h"
#include "simulation_core_api.h"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
static uint64_t g_tick=0;
#define SEC(n) do{g_tick=0; printf("\n=== %s ===\n", n);}while(0)
static void init_test_ions(SegmentSnapshot* s){
    s->intra_ions[0]=15.0f; s->intra_ions[1]=140.0f; s->intra_ions[2]=0.0001f;
    s->intra_ions[3]=5.0f; s->intra_ions[4]=0.5f; s->intra_ions[5]=151.0f;
    s->extra_ions[0]=140.0f; s->extra_ions[1]=5.0f; s->extra_ions[2]=1.2f;
    s->extra_ions[3]=130.0f; s->extra_ions[4]=2.0f; s->extra_ions[5]=0.0f;
}
#define NMLDIR "/home/vitaf/workarea/develop/MindCopy/examples/neuroml"

static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i * core_seg_size();
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}
static void rtk(CoreData d,int n,float dt=0.1f){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&g_tick,dt,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}

int main(){
    core_init_libraries(); core_set_bio_mode();

    SEC("1. SPIKE DETECTION: пороговая детекция");
    {
        NetworkModel m; m.segments.reserve(16); auto*s=m.create_segment({0,0,0},SegmentType::SOMA); init_test_ions(s);
        s->V_m=-70; s->radius=10; s->length=20;
        s->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        s->add_modifier(ModifierType::MOD_IONOTROPIC_CL,0.1f);
        s->add_modifier(ModifierType::MOD_K2P_LEAK,0.03f);

        SaveNetwork(m,"/tmp/tspike.bion"); CoreData d=core_load_bion("/tmp/tspike.bion");
        core_add_current_stimulus(s->id,800,10,10);

        // Запись V_m каждые 10 тиков
        std::vector<float> trace;
        for(int i=0;i<500;i++){
            rtk(d,1); if(i%5==0) trace.push_back(rvm(d,s->id));
        }
        core_unload_bion(d); remove("/tmp/tspike.bion");

        // Детекция спайков (пересечение -20 mV)
        int spikes=0; bool above=false;
        for(size_t i=1;i<trace.size();i++){
            if(trace[i]>-20&&!above){spikes++;above=true;}
            if(trace[i]<-40) above=false;
        }
        float vmin=trace[0],vmax=trace[0];
        for(auto v:trace){if(v<vmin)vmin=v;if(v>vmax)vmax=v;}
        printf("  Vm: %.1f..%.1f mV, %zu точек, спайков: %d\n",vmin,vmax,trace.size(),spikes);
        CHK(trace.size()==100,"Trace length: %zu",trace.size());
        CHK(vmax>-40,"Depolarization above -40mV: %.1f",vmax);
    }

    SEC("2. MULTI-NEURON CABLE: propagation عبر осевых связей");
    {
        NetworkModel m; m.segments.reserve(16);
        auto*s1=m.create_segment({0,0,0},SegmentType::SOMA); init_test_ions(s1); s1->V_m=-70;s1->radius=10;s1->length=20;
        auto*s2=m.create_segment({0,30,0},SegmentType::DENDRITE,{0,1,0}); init_test_ions(s2); s2->V_m=-70;s2->radius=3;s2->length=60;
        auto*s3=m.create_segment({0,100,0},SegmentType::DENDRITE,{0,1,0}); init_test_ions(s3); s3->V_m=-70;s3->radius=2;s3->length=80;
        s1->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        s1->add_modifier(ModifierType::MOD_IONOTROPIC_CL,0.1f);
        IntraConnection c1={0,s1->id,s2->id},c2={0,s2->id,s3->id};
        m.connections.push_back(c1); m.connections.push_back(c2);

        SaveNetwork(m,"/tmp/tcable.bion"); CoreData d=core_load_bion("/tmp/tcable.bion");
        core_add_current_stimulus(s1->id,800,10,10);
        rtk(d,500);
        float v1=rvm(d,s1->id),v2=rvm(d,s2->id),v3=rvm(d,s3->id);
        core_unload_bion(d); remove("/tmp/tcable.bion");

        printf("  Vm: soma=%.1f dend1=%.1f dend2=%.1f\n",v1,v2,v3);
        // Проверка: дендрит получил часть деполяризации от сомы
        float delta2=fabsf(v2+70), delta3=fabsf(v3+70);
        CHK(delta2>0.05f||delta3>0.05f,"Cable propagation: d2=%.2f d3=%.2f mV",delta2,delta3);
    }

    SEC("3. ADAPTATION: repetitive firing fatigue");
    {
        NetworkModel m; m.segments.reserve(16); auto*s=m.create_segment({0,0,0},SegmentType::SOMA); init_test_ions(s);
        s->V_m=-70;s->radius=10;s->length=20;
        s->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        s->add_modifier(ModifierType::MOD_IONOTROPIC_CL,0.1f);
        s->add_modifier(ModifierType::MOD_K2P_LEAK,0.03f);

        SaveNetwork(m,"/tmp/tadapt.bion"); CoreData d=core_load_bion("/tmp/tadapt.bion");
        // Длительная стимуляция: 2000 тиков
        core_add_current_stimulus(s->id,300,100,10);
        std::vector<float> trace;
        for(int i=0;i<2000;i++){
            rtk(d,1); if(i%20==0) trace.push_back(rvm(d,s->id));
        }
        core_unload_bion(d); remove("/tmp/tadapt.bion");

        // Проверка: V_m не уходит в бесконечность
        float vmax=trace[0];
        for(auto v:trace) if(v>vmax) vmax=v;
        CHK(vmax<=150.0f,"V_m bounded: max=%.1f mV",vmax);
        CHK(vmax>-50,"Depolarization detected: %.1f mV",vmax);
        CHK(trace.size()==100,"Trace: %zu points",trace.size());
    }

    SEC("4. SYNAPSE ACTIVATION: pre→post передача");
    {
        // Правильная архитектура: soma→axon→PRE:::POST→target (как в debug_network)
        NetworkModel m; m.segments.reserve(16);
        // Пресинаптический нейрон: сома + аксон
        auto*pre_soma=m.create_segment({0,0,0},SegmentType::SOMA); init_test_ions(pre_soma); pre_soma->V_m=-70;pre_soma->radius=5;pre_soma->length=10;
        pre_soma->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        pre_soma->add_modifier(ModifierType::MOD_K2P_LEAK,0.03f);
        uint32_t pre_soma_id=pre_soma->id;
        auto*pre_axon=m.create_segment({0,-15,0},SegmentType::AXON); init_test_ions(pre_axon); pre_axon->V_m=-70;pre_axon->radius=2;pre_axon->length=15;
        pre_axon->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        uint32_t pre_axon_id=pre_axon->id;
        // IntraConnection: soma→axon
        IntraConnection c1={0,pre_soma_id,pre_axon_id,1}; m.connections.push_back(c1);
        // PRE bouton
        auto*pre_bouton=m.create_segment({0,-30,0},SegmentType::PRESYNAPSE); init_test_ions(pre_bouton); pre_bouton->V_m=-55;pre_bouton->radius=2.5f;pre_bouton->length=15;
        pre_bouton->add_modifier(ModifierType::MOD_SYNAPTIC_RELEASE,0.9f);
        uint32_t pre_bouton_id=pre_bouton->id;
        IntraConnection c2={0,pre_axon_id,pre_bouton_id,1}; m.connections.push_back(c2);
        // Постсинаптическая цель
        auto*post=m.create_segment({20,0,0},SegmentType::DENDRITE); init_test_ions(post); post->V_m=-70;post->radius=5;post->length=10;
        post->add_modifier(ModifierType::MOD_KCC2,0.7f);
        uint32_t post_id=post->id;
        // POST spine
        auto*post_spine=m.create_segment({18,0,0},SegmentType::POSTSYNAPSE); init_test_ions(post_spine); post_spine->V_m=-62;post_spine->radius=2.5f;post_spine->length=15;
        post_spine->add_modifier(ModifierType::MOD_KCC2,0.7f);
        uint32_t post_spine_id=post_spine->id;
        IntraConnection c3={0,post_spine_id,post_id,1}; m.connections.push_back(c3);
        // Химический синапс PRE→POST
        auto*syn=m.create_synapse(pre_bouton_id,post_spine_id,SynapseType::CHEMICAL);
        if(syn){syn->weight=3.0f;syn->delay=1.0f;syn->tau_clearance=14.0f;
            syn->post_receptors[0].type = static_cast<uint8_t>(ModifierType::MOD_NT_RECEPTOR_AMPA);
            syn->post_receptors[0].density=3.0f;syn->post_receptor_count=1;
            syn->release_probability=0.7f;syn->release_amount=1.2f;
            syn->pre_area=1.5f;syn->post_area=1.5f;}

        SaveNetwork(m,"/tmp/tsynact.bion"); CoreData d=core_load_bion("/tmp/tsynact.bion");
        core_add_current_stimulus(pre_soma_id,800,10,5);
        rtk(d,400);
        float v_pre=rvm(d,pre_soma_id),v_post=rvm(d,post_id);
        core_unload_bion(d); remove("/tmp/tsynact.bion");

        printf("  Vm: pre=%.1f post=%.1f\n",v_pre,v_post);
        CHK(fabsf(v_post+70)>0.1f,"Postsynaptic response: %.2f mV",fabsf(v_post+70));
    }

    SEC("5. ION ACCUMULATION: Na/K gradients");
    {
        NetworkModel m; m.segments.reserve(16); auto*s=m.create_segment({0,0,0},SegmentType::SOMA); init_test_ions(s);
        s->V_m=-70;s->radius=10;s->length=20;
        s->add_modifier(ModifierType::MOD_AXON_NAV12,15.6f);
        s->intra_ions[0]=15;s->extra_ions[0]=150;s->intra_ions[1]=140;s->extra_ions[1]=5;

        SaveNetwork(m,"/tmp/tions.bion"); CoreData d=core_load_bion("/tmp/tions.bion");
        core_add_current_stimulus(s->id,500,20,5);
        rtk(d,500);
        // Read ions back (core API)
        float vm=rvm(d,s->id);
        core_unload_bion(d); remove("/tmp/tions.bion");

        CHK(fabsf(vm+70)>0.5f,"Ion dynamics: Vm=%.1f after stimulation",vm);
    }

    SEC("6. NETWORK IMPORT + FULL SIM: DendConn");
    {
        char p[512]; snprintf(p,sizeof(p),NMLDIR"/networks/DendConn.net.nml");
        NetworkModel m; m.segments.reserve(16); std::string e;
        bool ok=ImportNeuroML(p,m,e);
        CHK(ok,"Import DendConn: %s",ok?"OK":e.c_str());
        if(ok){
            SaveNetwork(m,"/tmp/tdend.bion"); CoreData d=core_load_bion("/tmp/tdend.bion");
            uint32_t sid=m.segments[0].id;
            float v0=rvm(d,sid);
            core_clear_stimuli();
            core_add_current_stimulus(sid,2000,100,10); rtk(d,500);
            float v1=rvm(d,sid);
            core_unload_bion(d); remove("/tmp/tdend.bion");
            printf("  DendConn soma: %.1f -> %.1f mV (d=%.1f)\n",v0,v1,v1-v0);
            CHK(fabsf(v1-v0)>2.0f,"DendConn stimulated: d=%.1f mV",v1-v0);
            CHK(m.synapses.size()>=4,"DendConn synapses: %zu",m.synapses.size());
            CHK(m.populations.size()>=1,"DendConn populations: %zu",m.populations.size());
        }
    }

    int t=P+F; printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
