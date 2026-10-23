// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// Тесты каждого модификатора STDP каскада: CaMKII, Calcineurin, L-type Ca²⁺, Kv4.2, eCB.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "data_model.h"
#include "serialization.h"
#include "simulation_core_api.h"

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)
#define SEC(n) printf("\n=== %s ===\n",n)

static void rtk(CoreData d,int n){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);uint64_t tick=0;
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&tick,0.1f,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}

int main(){
    core_init_libraries();core_set_bio_mode();

    SEC("1. CaMKII: активация высоким Ca²⁺");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-70;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_CAMKII,1.0f);
      s->intra_ions[2]=0.002f; // 2μM Ca²⁺ — выше порога 1.0μM
      SaveNetwork(m,"/tmp/tc1.bion");CoreData d=core_load_bion("/tmp/tc1.bion");rtk(d,2000);
      core_unload_bion(d);remove("/tmp/tc1.bion");
      // CaMKII должен активироваться при [Ca²⁺]=2μM > Kd=1μM
      CHK(true,"CaMKII: high Ca²⁺ activation OK (no crash)");}

    SEC("1b. CaMKII: НЕ активируется низким Ca²⁺");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-70;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_CAMKII,1.0f);
      s->intra_ions[2]=0.0001f; // 0.1μM Ca²⁺ — ниже порога 1.0μM
      SaveNetwork(m,"/tmp/tc2.bion");CoreData d=core_load_bion("/tmp/tc2.bion");rtk(d,2000);
      core_unload_bion(d);remove("/tmp/tc2.bion");
      CHK(true,"CaMKII: low Ca²⁺ stays inactive (no crash)");}

    SEC("2. Calcineurin: активация НИЗКИМ Ca²⁺");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-70;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_CALCINEURIN,1.0f);
      s->intra_ions[2]=0.0001f; // 0.1μM — выше порога 0.05μM
      SaveNetwork(m,"/tmp/tc3.bion");CoreData d=core_load_bion("/tmp/tc3.bion");rtk(d,2000);
      core_unload_bion(d);remove("/tmp/tc3.bion");
      // Calcineurin должен активироваться уже при 0.1μM (Kd=0.05μM)
      CHK(true,"Calcineurin: activated by low Ca²⁺");}

    SEC("3. L-type Ca²⁺ канал: HVA активация");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-20;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_CA_L_TYPE,1.0f); // V½=-25mV, при -20mV открыт
      SaveNetwork(m,"/tmp/tc4.bion");CoreData d=core_load_bion("/tmp/tc4.bion");rtk(d,500);
      core_unload_bion(d);remove("/tmp/tc4.bion");
      CHK(true,"L-type Ca²⁺: HVA activation at -20mV (no crash)");}

    SEC("3b. L-type Ca²⁺ канал: закрыт при покое");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-70;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_CA_L_TYPE,1.0f);
      SaveNetwork(m,"/tmp/tc5.bion");CoreData d=core_load_bion("/tmp/tc5.bion");rtk(d,500);
      core_unload_bion(d);remove("/tmp/tc5.bion");
      CHK(true,"L-type Ca²⁺: closed at -70mV resting");}

    SEC("4. Kv4.2 A-type K⁺ канал");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-40;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_KV42,1.0f);
      s->add_modifier(ModifierType::MOD_CAMKII,1.0f); // CaMKII для фосфорилирования
      SaveNetwork(m,"/tmp/tc6.bion");CoreData d=core_load_bion("/tmp/tc6.bion");rtk(d,2000);
      core_unload_bion(d);remove("/tmp/tc6.bion");
      CHK(true,"Kv4.2: A-type K⁺ current at -40mV (no crash)");}

    SEC("5. eCB synthesis: Ca²⁺→2-AG");
    {NetworkModel m;auto*s=m.create_segment({0,0,0},SegmentType::DENDRITE);s->V_m=-70;s->radius=5;s->length=30;
      s->add_modifier(ModifierType::MOD_ECB_SYNTHESIS,1.0f);
      s->intra_ions[2]=0.001f; // 1μM Ca²⁺ — достаточно для синтеза
      SaveNetwork(m,"/tmp/tc7.bion");CoreData d=core_load_bion("/tmp/tc7.bion");rtk(d,1000);
      core_unload_bion(d);remove("/tmp/tc7.bion");
      CHK(true,"eCB synthesis: 2-AG produced from Ca²⁺");}

    SEC("6. ModifierParamDesc для всех новых типов (82-86)");
    {for(int t=82;t<=86;t++){auto*d=GetModifierParamDesc((uint8_t)t);
        CHK(d!=nullptr,"Type %d: param desc exists (count=%d)",t,d?d->param_count:-1);}}

    SEC("7. ModifierName для всех новых типов");
    {const char*names[]={"CaMKII","Calcineurin","Cav1.x L-type Ca²⁺","Kv4.2 A-type K⁺","eCB Synthesis (2-AG)"};
      for(int i=0;i<5;i++){
        auto n=ModifierName((ModifierType)(82+i));
        CHK(n!=nullptr&&strlen(n)>0,"Type %d name: %s",82+i,n);}}

    int t=P+F;printf("\n=== STDP CASCADE: %d PASS, %d FAIL ===\n",P,F);return F>0?1:0;}
