// Copyright (c) 2026 Фешин Виталий Александрович. STDP test: DendConn 3-neuron experiment.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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

static int P=0,F=0;
#define PASS(f,...) do{P++;printf("  [PASS] " f "\n",##__VA_ARGS__);}while(0)
#define FAIL(f,...) do{F++;printf("  [FAIL] " f "\n",##__VA_ARGS__);}while(0)
#define CHK(c,f,...) do{if(c)PASS(f,##__VA_ARGS__);else FAIL(f,##__VA_ARGS__);}while(0)

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
    if(core_seg_id(s)==tid){double*ions=intra?core_seg_intra_ions(s):core_seg_extra_ions(s);return (float)ions[ion_idx];}}return-1;}

int main(){
    core_init_libraries();core_set_bio_mode();mkdir(EXPDIR,0755);
    const char* exp_name="DendConn_3Neuron_Test";

    // 1. Import
    printf("=== 1. Import DendConn ===\n");
    NetworkModel m;std::string err;
    CHK(ImportNeuroML(NMLDIR"/networks/DendConn.net.nml",m,err),"Import OK: segs=%zu neurons=%zu",m.segments.size(),m.neurons.size());
    if(m.segments.empty()){FAIL("No segments");return 1;}

    // 2. Find segments on 3 different neurons
    uint32_t n1_dend=0,n2_soma=0,n3_dend=0;
    for(auto&s:m.segments){
        if(s.neuron_id==1&&s.type==SegmentType::DENDRITE&&s.length>50&&!n1_dend)n1_dend=s.id;
        if(s.neuron_id==2&&s.type==SegmentType::SOMA&&!n2_soma)n2_soma=s.id;
        if(s.neuron_id==3&&s.type==SegmentType::DENDRITE&&s.length>30&&!n3_dend)n3_dend=s.id;
    }
    CHK(n1_dend>0&&n2_soma>0&&n3_dend>0,"Segments: N1_dend=%u N2_soma=%u N3_dend=%u",n1_dend,n2_soma,n3_dend);

    // 3. Save model
    char bion_path[512];snprintf(bion_path,sizeof(bion_path),"%s/%s.bion",EXPDIR,exp_name);
    CHK(SaveNetwork(m,bion_path),"Save BION");
    CoreData d=core_load_bion(bion_path);CHK(d!=nullptr,"Core load");

    // 4. Probes config (6 probes, 3 neurons)
    std::vector<ExperimentProbeConfig> probes(6);
    // E1: stimulating electrode on N1 dendrite
    probes[0].type=ProbeType::ELECTRODE;probes[0].segment_id=n1_dend;probes[0].placement=-1;
    strncpy(probes[0].label,"E1 Stim N1",31);probes[0].record_vm=true;probes[0].channels=1;
    probes[0].stim_enabled=true;probes[0].stim_amplitude_pA=300;probes[0].stim_duration_ms=5;probes[0].stim_type=1; // ramp
    probes[0].color[0]=0.9f;probes[0].color[1]=0.2f;probes[0].color[2]=0.2f;
    // E2: recording electrode on N2 soma
    probes[1].type=ProbeType::ELECTRODE;probes[1].segment_id=n2_soma;probes[1].placement=-1;
    strncpy(probes[1].label,"E2 Rec N2",31);probes[1].record_vm=true;probes[1].channels=1;
    probes[1].color[0]=0.2f;probes[1].color[1]=0.4f;probes[1].color[2]=1.0f;
    // C3: intracellular cannula injecting K+ only (not recording V_m)
    probes[2].type=ProbeType::CANNULE;probes[2].segment_id=n3_dend;probes[2].placement=1;
    strncpy(probes[2].label,"C3 Inj K+ [int]",31);probes[2].record_vm=false;probes[2].channels=0;
    probes[2].inject_enabled=true;probes[2].inject_substance=1;probes[2].inject_amount=10.0f;probes[2].color[0]=1.0f;probes[2].color[1]=0.5f;probes[2].color[2]=0.1f;
    // C4: extracellular cannula injecting Na+ only (not recording V_m)
    probes[3].type=ProbeType::CANNULE;probes[3].segment_id=n3_dend;probes[3].placement=0;
    strncpy(probes[3].label,"C4 Inj Na+ [ext]",31);probes[3].record_vm=false;probes[3].channels=0;
    probes[3].inject_enabled=true;probes[3].inject_substance=0;probes[3].inject_amount=5.0f;probes[3].color[0]=0.2f;probes[3].color[1]=0.6f;probes[3].color[2]=1.0f;
    // IS5: intracellular ion-selective microelectrode recording K+ on N3
    probes[4].type=ProbeType::ION_SELECTIVE;probes[4].segment_id=n3_dend;probes[4].placement=1;
    strncpy(probes[4].label,"IS5 Rec K+ [int]",31);probes[4].record_vm=false;
    probes[4].record_ions[1]=true;probes[4].channels=1;
    probes[4].color[0]=1.0f;probes[4].color[1]=0.5f;probes[4].color[2]=0.1f;
    // IS6: extracellular ion-selective microelectrode recording Na+ on N3
    probes[5].type=ProbeType::ION_SELECTIVE;probes[5].segment_id=n3_dend;probes[5].placement=0;
    strncpy(probes[5].label,"IS6 Rec Na+ [ext]",31);probes[5].record_vm=false;
    probes[5].record_ions[0]=true;probes[5].channels=1;
    probes[5].color[0]=0.2f;probes[5].color[1]=0.6f;probes[5].color[2]=1.0f;

    // 5. Simulation with ramp stimulus + K+ intra + Na+ extra injection
    printf("=== 2. Simulation ===\n");
    int total_ticks=3000,step=15,rec_pts=total_ticks/step;
    // channels: tick + E1_vm + E2_vm + IS5_K_intra + IS6_Na_extra = 5
    int channels=5;
    std::vector<float> frames;frames.reserve(rec_pts*channels);

    for(int t=0;t<total_ticks;t++){
        float ramp_amp = (t<1000) ? (t*0.3f) : 300.0f;
        core_add_current_stimulus(n1_dend,ramp_amp,5,t);
        if(t%100==0){
            core_add_ion_injection(n3_dend,1,10.0f,0); // C3: K+ intracellular
            core_add_ion_injection(n3_dend,0,5.0f,1);  // C4: Na+ extracellular
        }
        rtk(d,1);
        if(t%step==0){
            frames.push_back((float)t);               // 0: tick
            frames.push_back(rvm(d,n1_dend));          // 1: E1 Vm
            frames.push_back(rvm(d,n2_soma));          // 2: E2 Vm
            frames.push_back(rion(d,n3_dend,1,true));  // 3: IS5 K+ intra
            frames.push_back(rion(d,n3_dend,0,false)); // 4: IS6 Na+ extra
        }
    }
    core_unload_bion(d);

    // 6. Verify data
    printf("=== 3. Validation ===\n");
    CHK(rec_pts==200,"Recording points: %d",rec_pts);
    CHK(frames.size()==(size_t)rec_pts*channels,"Frames: %zu",frames.size());

    // Check V_m changes (stimulation should depolarize N1)
    float v_first=frames[1],v_last=frames[(rec_pts-1)*channels+1];
    CHK(fabsf(v_last-v_first)>5.0f,"V_m N1 changed: %.1f→%.1f mV",v_first,v_last);

    // Check Na+ extracellular rises with injections (normal ~150mM)
    float na_first=frames[4];
    CHK(na_first>130&&na_first<170,"Na+ extra start: %.1f mM",na_first);
    float na_last=frames[(rec_pts-1)*channels+4];
    CHK(na_last>na_first+2.0f,"Na+ extra rise: %.1f → %.1f mM (Δ=%.1f)",na_first,na_last,na_last-na_first);

    // Check K+ intracellular rises visibly (normal ~140mM)
    float k_first=frames[3];
    CHK(k_first>100&&k_first<160,"K+ intra start: %.1f mM",k_first);
    float k_last=frames[(rec_pts-1)*channels+3];
    CHK(k_last>k_first+5.0f,"K+ intra rise: %.1f → %.1f mM (Δ=%.1f)",k_first,k_last,k_last-k_first);

    // 7. Save .bexp + index.json
    printf("=== 4. Save ===\n");
    std::string json="{\n  \"name\": \""+std::string(exp_name)+"\",\n  \"description\": \"DendConn 3-neuron ramp+K+intra+Na+extra test\",\n  \"date\": \"2026-06-01\",\n  \"model_name\": \"DendConn.net.nml\",\n  \"bion_file_path\": \""+std::string(bion_path)+"\",\n  \"cell_type\": \"Pyramidal\",\n  \"probe_count\": 6,\n  \"tick_count\": "+std::to_string(rec_pts)+",\n  \"probes\": [\n";
    for(size_t i=0;i<probes.size();i++){auto&p=probes[i];char buf[512];
        snprintf(buf,sizeof(buf),"    {\"type\":\"%s\",\"segment_id\":%u,\"label\":\"%s\",\"record_vm\":%s,\"record_ions\":[%s,%s,%s,%s,%s],\"channels\":%u,\"placement\":%d,\"inject_enabled\":%s,\"inject_is_ion\":%s,\"inject_substance\":%d,\"inject_amount\":%.4f,\"stim_enabled\":%s,\"stim_amplitude_pA\":%.1f,\"stim_duration_ms\":%.1f,\"stim_delay_ms\":%.1f,\"stim_type\":%d,\"color\":[%.2f,%.2f,%.2f]}%s\n",
            p.type==ProbeType::CANNULE?"CANNULA":(p.type==ProbeType::ION_SELECTIVE?"ION_SELECTIVE":"ELECTRODE"),
            p.segment_id,p.label,p.record_vm?"true":"false",
            p.record_ions[0]?"true":"false",p.record_ions[1]?"true":"false",p.record_ions[2]?"true":"false",p.record_ions[3]?"true":"false",p.record_ions[4]?"true":"false",
            p.channels,p.placement,p.inject_enabled?"true":"false",p.inject_is_ion?"true":"false",
            p.inject_substance,p.inject_amount,
            p.stim_enabled?"true":"false",p.stim_amplitude_pA,p.stim_duration_ms,p.stim_delay_ms,p.stim_type,
            p.color[0],p.color[1],p.color[2],i<probes.size()-1?",":"");json+=buf;}
    json+="  ]\n}\n";

    char bexp_path[512];snprintf(bexp_path,sizeof(bexp_path),"%s/%s.bexp",EXPDIR,exp_name);
    FILE*f=fopen(bexp_path,"wb");CHK(f!=nullptr,"Create .bexp");
    uint32_t magic=BEXP_MAGIC,ver=BEXP_VERSION,jl=json.size();
    fwrite(&magic,4,1,f);fwrite(&ver,4,1,f);fwrite(&jl,4,1,f);fwrite(json.c_str(),1,jl,f);
    uint32_t fc=rec_pts,fs=channels,fb=frames.size()*4;
    fwrite(&fc,4,1,f);fwrite(&fs,4,1,f);fwrite(&fb,4,1,f);fwrite(frames.data(),1,fb,f);
    uint32_t ne=0;fwrite(&ne,4,1,f);fclose(f);
    CHK(access(bexp_path,F_OK)==0,"BEXP saved: %s",bexp_path);

    // Используем ExperimentManager для атомарного обновления index.json
    // вместо прямого fopen("w") — чтобы не перезатереть другие эксперименты
    ExperimentManager mgr;
    mgr.scanIndex(EXPDIR);
    // Проверяем, что наш .bexp попал в индекс
    bool found_in_index = false;
    for (const auto& e : mgr.indexEntries()) {
        if (e.file_path == std::string(exp_name) + ".bexp") { found_in_index = true; break; }
    }
    CHK(found_in_index, "Experiment in index after scanIndex");

    // 8. Load back and validate
    printf("=== 5. Load & verify ===\n");
    ExperimentData loaded;std::string load_err;
    // Use low-level read to verify binary integrity
    FILE*bf=fopen(bexp_path,"rb");CHK(bf!=nullptr,"Reopen .bexp");
    uint32_t rmagic=0,rver=0,rjl=0;fread(&rmagic,4,1,bf);fread(&rver,4,1,bf);fread(&rjl,4,1,bf);
    CHK(rmagic==BEXP_MAGIC,"BEXP magic OK");
    CHK(rver==BEXP_VERSION,"BEXP version OK");
    fseek(bf,rjl,SEEK_CUR); // skip JSON header
    uint32_t rfc=0,rfs=0,rfbytes=0;fread(&rfc,4,1,bf);fread(&rfs,4,1,bf);fread(&rfbytes,4,1,bf);
    CHK(rfc==rec_pts,"Frame count: %u",rfc);
    CHK(rfs==channels,"Frame stride: %u",rfs);
    CHK(rfbytes==frames.size()*4,"Frame bytes: %u",rfbytes);
    // Verify JSON contains injection info for cannula probe
    fseek(bf,12,SEEK_SET); // back to JSON after magic+ver+len
    std::string json_str(rjl,'\0');fread(&json_str[0],1,rjl,bf);
    CHK(json_str.find("inject_enabled")!=std::string::npos,"JSON has inject_enabled");
    CHK(json_str.find("\"C3 Inj K+ [int]\"")!=std::string::npos,"JSON has C3 K+ cannula label");
    CHK(json_str.find("\"C4 Inj Na+ [ext]\"")!=std::string::npos,"JSON has C4 Na+ cannula label");
    CHK(json_str.find("ION_SELECTIVE")!=std::string::npos,"JSON has ION_SELECTIVE type");
    CHK(json_str.find("\"IS5 Rec K+ [int]\"")!=std::string::npos,"JSON has IS5 K+ probe");
    CHK(json_str.find("\"IS6 Rec Na+ [ext]\"")!=std::string::npos,"JSON has IS6 Na+ probe");
    // Новые поля
    CHK(json_str.find("bion_file_path")!=std::string::npos,"JSON has bion_file_path");
    CHK(json_str.find("stim_delay_ms")!=std::string::npos,"JSON has stim_delay_ms");
    CHK(json_str.find("inject_is_ion")!=std::string::npos,"JSON has inject_is_ion");
    CHK(json_str.find("stim_type")!=std::string::npos,"JSON has stim_type");
    // Проверяем все 5 ионов (Mg должен быть)
    CHK(json_str.find("\"record_ions\": [true, false, false, false, false]")!=std::string::npos
       || json_str.find("\"record_ions\":[true,false,false,false,false]")!=std::string::npos,
       "JSON record_ions has 5 elements");
    fclose(bf);

    // 9. Проверка ResolveBionPath из загруженного .bexp
    printf("=== 6. ResolveBionPath from .bexp ===\n");
    {
        ExperimentData loaded_data;
        std::string ler;
        // Используем ExperimentManager для загрузки
        ExperimentManager mgr;
        bool lOk = mgr.loadFromFile(bexp_path, loaded_data, ler);
        CHK(lOk, "ExperimentManager::loadFromFile OK");
        if (lOk) {
            CHK(!loaded_data.meta.bion_file_path.empty(), "bion_file_path loaded: %s",
                loaded_data.meta.bion_file_path.c_str());
            CHK(loaded_data.meta.bion_file_path == bion_path,
                "bion_file_path matches saved path");

            // Проверяем ConfigToProbe на загруженных данных
            CHK(!loaded_data.probes.empty(), "Probes loaded: %zu", loaded_data.probes.size());
            if (!loaded_data.probes.empty()) {
                Probe p = ExperimentManager::ConfigToProbe(loaded_data.probes[0]);
                CHK(p.type == loaded_data.probes[0].type, "ConfigToProbe type match");
                CHK(p.segment_id == loaded_data.probes[0].segment_id,
                    "ConfigToProbe seg_id match: %u", p.segment_id);
                CHK(p.stim_enabled == loaded_data.probes[0].stim_enabled,
                    "ConfigToProbe stim_enabled: %d", p.stim_enabled);
            }
        }
    }

    // 10. Проверка saveAsTemplate
    printf("=== 7. saveAsTemplate ===\n");
    {
        ExperimentData loaded_data;
        std::string ler;
        ExperimentManager mgr;
        if (mgr.loadFromFile(bexp_path, loaded_data, ler)) {
            char tmpl_path[512];
            snprintf(tmpl_path, sizeof(tmpl_path), "%s/%s.bexp_template", EXPDIR, exp_name);
            bool tOk = mgr.saveAsTemplate(tmpl_path, loaded_data);
            CHK(tOk, "saveAsTemplate OK");
            CHK(access(tmpl_path, F_OK) == 0, "Template file exists");

            // Проверяем что шаблон загружается обратно
            ExperimentData tmpl_data;
            std::string tmpl_err;
            bool lOk = mgr.loadFromFile(tmpl_path, tmpl_data, tmpl_err);
            CHK(lOk, "Load template back OK");
            if (lOk) {
                CHK(tmpl_data.meta.tick_count == 0u, "Template tick_count=0 after load");
                CHK(tmpl_data.probes.size() == loaded_data.probes.size(),
                    "Template probes preserved: %zu", tmpl_data.probes.size());
                // Имя шаблона должно сохраниться
                CHK(tmpl_data.meta.name == exp_name, "Template name preserved");
            }
            unlink(tmpl_path);
        }
    }

    int t=P+F;printf("\n=== DENDRONN EXP TEST: %d PASS, %d FAIL (%d%%) ===\n",P,F,t>0?P*100/t:0);
    return F>0?1:0;
}
