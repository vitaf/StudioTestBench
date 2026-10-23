// Copyright (c) 2026 Фешин Виталий Александрович. Создание .bexp эксперимента.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <sys/stat.h>
#include "neuroml_import.h"
#include "serialization.h"
#include "data_model.h"
#include "experiment_data.h"
#include "experiment_manager.h"
#include "simulation_core_api.h"
#ifndef MINDCopy_ROOT
#define MINDCopy_ROOT "/home/vitaf/workarea/develop/MindCopy"
#endif
#define NMLDIR MINDCopy_ROOT "/examples/neuroml"
#ifndef STUDIO_HOME
#define STUDIO_HOME "/home/vitaf/BioNeuronStudio"
#endif
#define EXPDIR STUDIO_HOME "/experiments"

static void rtk(CoreData d,int n){void*st,*sc,*vc,*vcc,*rm,*rmc,*io,*ioc,*li,*lic; void*me,*mec;
  core_get_stimulus_buffers(&st,&sc,&vc,&vcc,&rm,&rmc,&io,&ioc,&li,&lic, &me, &mec);uint64_t tick=0;
  for(int t=0;t<n;t++)core_run_tick(core_segments_ptr(d),core_num_segments(d),
    core_connections_ptr(d),core_num_connections(d),core_synapses_ptr(d),core_num_synapses(d),
    0,&tick,0.1f,st,sc,vc,vcc,rm,rmc,io,ioc,li,lic, me, mec);}
static float rvm(CoreData d,uint32_t tid){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i * core_seg_size();
    if(core_seg_id(s)==tid)return core_seg_vm(s);}return-999;}
static float rion(CoreData d,uint32_t tid,int ion_idx,bool intra){uint32_t n=core_num_segments(d);
  for(uint32_t i=0;i<n;i++){void*s=(char*)core_segments_ptr(d)+i * core_seg_size();
    if(core_seg_id(s)==tid){float*ions=intra?core_seg_intra_ions(s):core_seg_extra_ions(s);return ions[ion_idx];}}return-1;}

static std::string jesc(const std::string& s){std::string o;for(char c:s){if(c=='"'||c=='\\')o+='\\';o+=c;}return o;}

static bool write_bexp(const char* path, const ExperimentMeta& meta,
    const std::vector<ExperimentProbeConfig>& probes, const std::vector<ExperimentEvent>& events,
    const std::vector<float>& frames, int channels){
    FILE*f=fopen(path,"wb");if(!f)return false;
    std::string json="{\n  \"name\": \""+jesc(meta.name)+"\",\n  \"description\": \""+jesc(meta.description)+"\",\n  \"date\": \""+jesc(meta.date)+"\",\n  \"model_name\": \""+jesc(meta.model_name)+"\",\n  \"cell_type\": \""+jesc(meta.cell_type)+"\",\n  \"probe_count\": "+std::to_string(probes.size())+",\n  \"tick_count\": "+std::to_string(meta.tick_count)+",\n  \"probes\": [\n";
    for(size_t i=0;i<probes.size();i++){auto&p=probes[i];char buf[512];
        snprintf(buf,sizeof(buf),"    {\"type\":\"%s\",\"segment_id\":%u,\"label\":\"%s\",\"record_vm\":%s,\"record_ions\":[%s,%s,%s,%s,%s],\"channels\":%u,\"placement\":%d,\"color\":[%.2f,%.2f,%.2f]}%s\n",
            p.type==ProbeType::CANNULE?"CANNULA":"ELECTRODE",p.segment_id,p.label,p.record_vm?"true":"false",
            p.record_ions[0]?"true":"false",p.record_ions[1]?"true":"false",p.record_ions[2]?"true":"false",p.record_ions[3]?"true":"false",p.record_ions[4]?"true":"false",
            p.channels,p.placement,p.color[0],p.color[1],p.color[2],i<probes.size()-1?",":"");json+=buf;}
    json+="  ]\n}\n";
    uint32_t magic=BEXP_MAGIC,version=BEXP_VERSION,jl=json.size();
    fwrite(&magic,4,1,f);fwrite(&version,4,1,f);fwrite(&jl,4,1,f);fwrite(json.c_str(),1,jl,f);
    uint32_t fc=meta.tick_count,fs=channels,fb=frames.size()*4;
    fwrite(&fc,4,1,f);fwrite(&fs,4,1,f);fwrite(&fb,4,1,f);fwrite(frames.data(),1,fb,f);
    uint32_t ne=events.size();fwrite(&ne,4,1,f);
    for(auto&e:events)fwrite(&e,sizeof(e),1,f);
    long fsize = ftell(f); fclose(f); printf("  Written: %s (%ld bytes)\n", path, fsize); return true;}

int main(){
    core_init_libraries();core_set_bio_mode();mkdir(EXPDIR,0755);
    const char* exp_name="DendConn_ProbeTest";

    printf("=== Import DendConn ===\n");
    NetworkModel m;std::string err;
    if(!ImportNeuroML(NMLDIR"/networks/DendConn.net.nml",m,err)){fprintf(stderr,"FAIL: %s\n",err.c_str());return 1;}
    printf("  segs=%zu neurons=%zu syns=%zu\n",m.segments.size(),m.neurons.size(),m.synapses.size());

    uint32_t stim_seg=0,rec_seg=0;
    for(auto&s:m.segments){
        if(s.type==SegmentType::DENDRITE&&s.length>50&&s.neuron_id==1&&!stim_seg)stim_seg=s.id;
        if(s.type==SegmentType::DENDRITE&&s.length>50&&s.neuron_id==2&&!rec_seg)rec_seg=s.id;
    }
    printf("  Stim seg=%u Rec seg=%u\n",stim_seg,rec_seg);

    // Пробы: все 5 ионов, placement, цвета
    std::vector<ExperimentProbeConfig> probes(4);
    probes[0].type=ProbeType::ELECTRODE;probes[0].segment_id=stim_seg; probes[0].placement=-1;
    strncpy(probes[0].label,"E1 Stim N0",31);probes[0].record_vm=true;probes[0].channels=1;
    probes[0].color[0]=0.9f;probes[0].color[1]=0.2f;probes[0].color[2]=0.2f;
    probes[1].type=ProbeType::CANNULE;probes[1].segment_id=stim_seg; probes[1].placement=0; // extracellular
    strncpy(probes[1].label,"C1 Stim N0 [ext]",31);probes[1].record_vm=true;probes[1].channels=1;
    probes[1].color[0]=1.0f;probes[1].color[1]=0.5f;probes[1].color[2]=0.1f;
    probes[1].cannula_placement=CannulaPlacement::EXTRACELLULAR;
    probes[2].type=ProbeType::ELECTRODE;probes[2].segment_id=rec_seg; probes[2].placement=-1;
    strncpy(probes[2].label,"E2 Rec N1",31);probes[2].record_vm=true;
    probes[2].record_ions[0]=true;probes[2].record_ions[1]=true;probes[2].record_ions[2]=true;probes[2].record_ions[3]=true;probes[2].record_ions[4]=true;
    probes[2].channels=6; // vm + 5 ions
    probes[2].color[0]=0.2f;probes[2].color[1]=0.4f;probes[2].color[2]=1.0f;
    probes[3].type=ProbeType::CANNULE;probes[3].segment_id=rec_seg; probes[3].placement=1; // intracellular
    strncpy(probes[3].label,"C2 Rec N1 [int]",31);probes[3].record_vm=true;
    probes[3].record_ions[0]=true;probes[3].record_ions[1]=true;probes[3].record_ions[2]=true;probes[3].record_ions[3]=true;probes[3].record_ions[4]=true;
    probes[3].channels=6; // vm + 5 ions
    probes[3].color[0]=0.2f;probes[3].color[1]=0.8f;probes[3].color[2]=0.3f;
    probes[3].cannula_placement=CannulaPlacement::INTRACELLULAR;

    ExperimentMeta meta;meta.name=exp_name;
    meta.description="DendConn: E1(кр)+C1(ор) N0 стим, E2(син)+C2(зел) N1 запись Vm/Na+/K+";
    meta.model_name="DendConn.net.nml";meta.cell_type="Пирамидный нейрон";
    meta.probe_count=4;meta.date="2026-06-01T12:00:00";

    char bion_path[512];snprintf(bion_path,sizeof(bion_path),"%s/%s.bion",EXPDIR,exp_name);
    if(!SaveNetwork(m,bion_path)){fprintf(stderr,"FAIL: save bion\n");return 1;}
    printf("  BION: %s\n",bion_path);

    printf("=== Simulation ===\n");
    CoreData d=core_load_bion(bion_path);if(!d){fprintf(stderr,"FAIL: core load\n");return 1;}
    int total_ticks=5000,step=25;
    // channels: tick + E1_vm + C1_vm + E2(vm+5ions) + C2(vm+5ions) = 1+1+1+6+6 = 15
    int channels=15;
    std::vector<float> frames;frames.reserve((total_ticks/step)*channels);

    for(int b=0;b<3;b++){core_add_current_stimulus(stim_seg,300,10,20+b*1500);core_add_ion_injection(stim_seg,1,2.0f,1);}
    for(int t=0;t<total_ticks;t++){rtk(d,1);
        if(t%step==0){
            frames.push_back((float)t);                  // 0: tick
            frames.push_back(rvm(d,stim_seg));            // 1: E1 Vm
            frames.push_back(rvm(d,stim_seg));            // 2: C1 Vm
            frames.push_back(rvm(d,rec_seg));             // 3: E2 Vm
            for(int ion=0;ion<5;ion++)                    // 4-8: E2 ions
                frames.push_back(rion(d,rec_seg,ion,ion==0?false:true)); // Na+extra, rest intra
            frames.push_back(rvm(d,rec_seg));             // 9: C2 Vm
            for(int ion=0;ion<5;ion++)                    // 10-14: C2 ions
                frames.push_back(rion(d,rec_seg,ion,true)); // all intracellular
        }
        if(t%1000==0)printf("  tick %d/%d\n",t,total_ticks);
    }
    core_unload_bion(d);meta.tick_count=total_ticks/step;
    std::vector<ExperimentEvent> events;
    for(int b=0;b<3;b++){ExperimentEvent e;e.tick=(uint32_t)(20+b*1500);e.probe_index=0;e.type=ExperimentEventType::STIM_START;e.params[0]=300;e.params[1]=10;events.push_back(e);}

    char bexp_path[512];snprintf(bexp_path,sizeof(bexp_path),"%s/%s.bexp",EXPDIR,exp_name);
    write_bexp(bexp_path,meta,probes,events,frames,channels);
    // Используем ExperimentManager для атомарного обновления index.json
    // вместо прямого fopen("w") — чтобы не перезатереть другие эксперименты
    ExperimentManager mgr;
    mgr.scanIndex(EXPDIR);
    printf("\n=== DONE: %s ===\n",exp_name);
    return 0;
}
