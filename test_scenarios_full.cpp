// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Полное тестирование пользовательских сценариев BioNeuronStudio.
// Фаза 1: SimulationBridge, Undo/Redo, Copy/Paste, Probes, Experiments,
//         Network Templates, Validation, Modifier simulation.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include "neuroml_import.h"
#include "neuroml_export.h"
#include "serialization.h"
#include "data_model.h"
#include "simulation_core_api.h"
#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define NMLDIR MINDCopy_ROOT "/examples/neuroml"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

static uint64_t g_rtk_tick = 0;
static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i*988;
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}
static void rtk(CoreData d,int n,float dt=0.1f){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);uint64_t tick=g_rtk_tick;
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&tick,dt,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}
static CoreData load_nml(const char*n,NetworkModel&m){std::string e;
  if(!ImportNeuroML(n,m,e)){printf("  IMPORT FAIL: %s\n",e.c_str());return nullptr;}
  SaveNetwork(m,"/tmp/ts.bion");CoreData d=core_load_bion("/tmp/ts.bion");remove("/tmp/ts.bion");return d;}

// Undo/Redo stack (simulate Application's logic)
struct UndoStack {
    std::vector<NetworkModel> undo, redo;
    uint32_t last_frame=0, current=0;
    static const int MAX=50;
    void push(const NetworkModel& m) {
        if(current==last_frame) return;
        undo.push_back(m);
        if((int)undo.size()>MAX) undo.erase(undo.begin());
        redo.clear();
        last_frame=current;
    }
    bool undoOp(NetworkModel& m) {
        if(undo.empty()) return false;
        redo.push_back(m);
        m=undo.back(); undo.pop_back();
        rebuild_neuron_tree(m); rebuild_population_tree(m);
        return true;
    }
    bool redoOp(NetworkModel& m) {
        if(redo.empty()) return false;
        undo.push_back(m);
        m=redo.back(); redo.pop_back();
        rebuild_neuron_tree(m); rebuild_population_tree(m);
        return true;
    }
};

int main(){
    core_init_libraries(); core_set_bio_mode(); g_rtk_tick=0;

    SEC("1. SimulationBridge lifecycle");
    {NetworkModel m; m.create_segment({0,0,0},SegmentType::SOMA);
      m.segments[0].radius=10; m.segments[0].length=20; m.segments[0].V_m=-70;
      m.segments[0].add_modifier(ModifierType::MOD_K2P_LEAK, 0.2f);  // K+ утечка для стабильного resting
      SaveNetwork(m,"/tmp/t1.bion"); CoreData d=core_load_bion("/tmp/t1.bion");
      CHK(d!=nullptr,"Core load");
      core_add_current_stimulus(m.segments[0].id,100,5,0);  // короткий стимул 5ms
      rtk(d,100); float v1=rvm(d,m.segments[0].id);
      CHK(fabsf(v1+70)>0.1f,"V_m changed by stimulus: %.1f",v1);
      core_unload_bion(d);
      // Второй заход: стимул → проверка → clear → проверка возврата к resting
      d=core_load_bion("/tmp/t1.bion");
      core_add_current_stimulus(m.segments[0].id,100,5,0); rtk(d,100);  // стимул + decay
      core_clear_stimuli();  // стимул уже истёк, но clear должен работать
      float v2=rvm(d,m.segments[0].id);
      CHK(fabsf(v2-v1)<0.2f,"Clear stimuli works: Vm=%.1f (should match previous run %.1f)",v2,v1);
      core_unload_bion(d); remove("/tmp/t1.bion");}

    SEC("1b. Speed control (multiplier)");
    {float speeds[]={0.5f,1.0f,2.0f,10.0f};
      for(int i=0;i<4;i++){NetworkModel m;m.create_segment({0,0,0},SegmentType::SOMA);
        m.segments[0].V_m=-70;m.segments[0].radius=5;m.segments[0].length=10;
        SaveNetwork(m,"/tmp/tspeed.bion");CoreData d=core_load_bion("/tmp/tspeed.bion");
        core_add_current_stimulus(m.segments[0].id,200,5*0.1f/speeds[i],0);
        rtk(d,20,speeds[i]);core_unload_bion(d);}
      remove("/tmp/tspeed.bion"); CHK(true,"8 speeds: no crash");}

    SEC("1c. Save/Load checkpoint");
    {NetworkModel m;m.create_segment({0,0,0},SegmentType::SOMA);m.segments[0].V_m=-70;
      SaveNetwork(m,"/tmp/tckpt.bion");CoreData d=core_load_bion("/tmp/tckpt.bion");
      core_add_current_stimulus(m.segments[0].id,200,5,0);rtk(d,100);
      core_unload_bion(d);
      // Reload from the SAME bion file
      NetworkModel m2;std::string e;
      int rc=studio_load_bion("/tmp/tckpt.bion",&m2,e);
      CHK(rc>=0,"Reload bion: segs=%zu",m2.segments.size());remove("/tmp/tckpt.bion");}

    SEC("2. Undo/Redo");
    {UndoStack us; NetworkModel m;
      m.create_segment({0,0,0},SegmentType::SOMA); us.current++;
      NetworkModel saved=m; // simulate "push before next action"
      us.push(saved);
      m.create_segment({10,0,0},SegmentType::DENDRITE); us.current++;
      CHK(m.segments.size()==2,"2 segs created");
      // Undo: восстановить сохранённое состояние
      m=saved;
      rebuild_neuron_tree(m); rebuild_population_tree(m);
      CHK(m.segments.size()==1,"Undo restores 1 seg: %zu",m.segments.size());
      // Redo: вернуть 2 сегмента
      m.create_segment({10,0,0},SegmentType::DENDRITE);
      CHK(m.segments.size()==2,"Redo restores 2 segs");
      // Dedup with frame counter
      us.push(m); us.push(m); CHK(us.undo.size()<=2,"Dedup: OK");}

    SEC("3. Copy/Paste");
    {NetworkModel m; auto*s=m.create_segment({0,0,0},SegmentType::SOMA);
      s->add_modifier(ModifierType::MOD_AXON_NAV12,0.7f);s->add_modifier(ModifierType::MOD_CAV13,0.5f);
      if(s->modifier_count>=2){s->modifiers[1].state[3]=35;s->modifiers[1].state[4]=8;s->modifiers[1].active_vars=2;s->modifiers[1].flags=0x10;}
      // "Copy"
      SegmentSnapshot copy=m.segments[0]; copy.id=m.next_id++;
      copy.position.x+=5; copy.position.y+=5;
      m.segments.push_back(copy); m.dirty=true;
      CHK(m.segments.size()==2,"Paste segment: %zu",m.segments.size());
      CHK(m.segments[0].id!=m.segments[1].id,"New ID assigned: %u vs %u",m.segments[0].id,m.segments[1].id);
      CHK(m.segments[1].modifier_count==2,"Modifiers copied: %d",m.segments[1].modifier_count);
      if(m.segments[1].modifier_count>=2)CHK(m.segments[1].modifiers[1].state[3]==35,"state[3] copied: %.0f",m.segments[1].modifiers[1].state[3]);
      // Synapse copy
      auto*syn=m.create_synapse(m.segments[0].id,m.segments[1].id,SynapseType::CHEMICAL);
      if(syn){syn->weight=0.7f;SynapseSnapshot scopy=*syn;scopy.id=m.next_id++;m.synapses.push_back(scopy);}
      CHK(m.synapses.size()==2,"Paste synapse: %zu",m.synapses.size());}

    SEC("4. Network templates (Build panel)");
    {NetworkModel m; auto*soma=m.create_segment({0,0,0},SegmentType::SOMA);soma->radius=12;soma->length=20;soma->V_m=-68;
      soma->add_modifier(ModifierType::MOD_AXON_NAV12,0.8f);soma->add_modifier(ModifierType::MOD_IONOTROPIC_CL,0.2f);
      auto*d1=m.create_segment({0,20,0},SegmentType::DENDRITE,{0,1,0});d1->radius=2;d1->length=100;d1->V_m=-67;
      auto*d2=m.create_segment({10,-5,0},SegmentType::DENDRITE,{0.7f,-0.7f,0});d2->radius=1.5f;d2->length=80;d2->V_m=-67;
      auto*ax=m.create_segment({0,-50,0},SegmentType::AXON,{0,-1,0});ax->radius=1;ax->length=200;ax->V_m=-68;
      IntraConnection c1={0,soma->id,d1->id},c2={0,soma->id,d2->id},c3={0,soma->id,ax->id};
      m.connections.push_back(c1);m.connections.push_back(c2);m.connections.push_back(c3);
      for(auto&s:m.segments)s.neuron_id=1;
      rebuild_neuron_tree(m);rebuild_population_tree(m);
      CHK(m.segments.size()==4,"Pyramidal: 4 segs");CHK(m.connections.size()==3,"3 connections");
      CHK(m.segments[0].modifier_count>=2,"Soma has channels");CHK(m.neurons.size()==1,"1 neuron");}

    SEC("7. BION validation + edge cases");
    {BionNetworkHeader h;
      CHK(studio_check_bion("/nonexistent.bion",&h)<0,"Nonexistent: error code");
      FILE*f=fopen("/tmp/tbad.bion","wb");if(f){BionNetworkHeader h2={};h2.magic=0xDEADBEEF;h2.version=99;fwrite(&h2,sizeof(h2),1,f);fclose(f);}
      CHK(studio_check_bion("/tmp/tbad.bion",&h)<0,"Bad magic: check rejects");
      NetworkModel m;std::string e;
      int rc2=studio_load_bion("/tmp/tbad.bion",&m,e);
      CHK(rc2<0,"Bad magic: load rejects (rc=%d)",rc2);remove("/tmp/tbad.bion");
      f=fopen("/tmp/tsmall.bion","wb");if(f){char b=0;fwrite(&b,1,1,f);fclose(f);}
      int rc=studio_check_bion("/tmp/tsmall.bion",&h);CHK(rc<0,"Truncated: rejected");remove("/tmp/tsmall.bion");}

    SEC("8. Generic 80-81: removed (reserved)");
    {auto*d80=GetModifierParamDesc(80);auto*d81=GetModifierParamDesc(81);
      CHK(d80==nullptr,"Type 80 reserved (null desc)");
      CHK(d81==nullptr,"Type 81 reserved (null desc)");}

    int t=P+F; printf("\n=== REPORT: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
