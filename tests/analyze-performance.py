#!/usr/bin/env python3
"""Offline saved-run performance evidence. No process, input, or game access."""
import argparse
import json
import math
import re
import statistics
from pathlib import Path

NUM=r'[-+0-9.eE]+'
PRESENT=re.compile(r'\[present-frame\] t=('+NUM+r') dt=('+NUM+r') gf=(\d+)')
ALLOWED_MANIFEST_DIFFERENCES={'game/winmm.dll','game/isaac-highfps.ini'}
# J374 input/update/render/GC/limiter bytes; independently audited against
# Validate19715Sites, not supplied by the probe's self-reported log markers.
NATIVE_ORIGINAL_SITES={
    'update_calls':(0x9295CF,'32c9e81a6412006a01e8f3340200'),
    'render_call':(0x9295DD,'e85e3d0200'),
    'gc_call':(0x9295EB,'e850bff3ff'),
    'limiter_test':(0x929611,'84db750c'),
    'limiter_branch':(0x92961B,'0f85f3feffff'),
}

def distribution(values):
    values=sorted(values)
    if not values:return None
    def percentile(p):
        x=(len(values)-1)*p;lo=int(x);hi=min(lo+1,len(values)-1)
        return values[lo]+(values[hi]-values[lo])*(x-lo)
    return {'count':len(values),'min':values[0],'mean':statistics.mean(values),'p50':percentile(.5),'p95':percentile(.95),'p99':percentile(.99),'max':values[-1]}


def census_mode(row):
    census=row.get('census')
    if not isinstance(census,dict):return 'invalid'
    if census.get('measured') is False:
        reason=census.get('reason')
        if not isinstance(reason,str) or not reason.strip():return 'invalid'
        if any(key in census for key in ('entities','effects')):return 'invalid'
        return 'not_measured'
    if 'measured' in census and census['measured'] is not True:return 'invalid'
    for key in ('entities','effects'):
        value=census.get(key)
        if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or value<0:
            return 'invalid'
    return 'measured'


def kv(text):
    return {k:float(v) for k,v in re.findall(r'(\w+)=('+NUM+r')',text)}

def parse_native(text):
    presents=[];costs=[];stats=[];workers=[];errors=[];latest=None
    for line in text.splitlines():
        match=PRESENT.search(line)
        if match:
            latest={'t':float(match[1]),'dt':float(match[2]),'gf':int(match[3])};presents.append(latest)
        if '[render-cost]' in line:
            row=kv(line);row['end_t']=latest['t'] if latest else None;costs.append(row)
        if '[stat]' in line:
            match=re.search(r'loop=(\d+)/s wrapper=(\d+)/s.*?iter avg ('+NUM+r') max ('+NUM+r').*?engine-upd avg ('+NUM+r') max ('+NUM+r').*?render~('+NUM+r').*?ours avg ('+NUM+r') max ('+NUM+r').*?hitches (\d+)',line)
            if match:
                stats.append(dict(zip(('loop_count','wrapper_count','iter_avg_ms','iter_max_ms','update_avg_ms','update_max_ms','residual_render_estimate_ms','own_avg_ms','own_max_ms','hitches'),map(float,match.groups())),end_t=latest['t'] if latest else None))
        if '[log-worker]' in line:workers.append(kv(line))
        if re.search(r'\[FATAL\]|\[present-error\]|\[pure-(?:lua|state)\]|retaining vanilla limiter|interpolation disabled|\[carousel-(?:restore|state)\]',line,re.I):errors.append(line)
    return presents,costs,stats,workers,errors

def parse_fixture(text):
    rows=[];errors=[]
    for line in text.splitlines():
        if '[HFPERF]' in line:
            try:rows.append(json.loads(line.split('[HFPERF]',1)[1].strip()))
            except (ValueError,TypeError):errors.append('Malformed HFPERF JSON')
        if re.search(r'Error in ["\'].+call|stack traceback|\[HFTEST\].*(?:FAILED|ERROR)',line,re.I):errors.append(line)
    return rows,errors

def manifest_difference(base,trial):
    if base is None or trial is None:return {'valid':False,'reason':'missing full case-manifest.json'}
    missing=sorted(set(base)-set(trial)-ALLOWED_MANIFEST_DIFFERENCES)
    added=sorted(set(trial)-set(base)-ALLOWED_MANIFEST_DIFFERENCES)
    changed=sorted(k for k in set(base)&set(trial)-ALLOWED_MANIFEST_DIFFERENCES if base[k]!=trial[k])
    return {'valid':not(missing or added or changed),'missing':missing,'added':added,'changed':changed,'allowed_changes':sorted(k for k in ALLOWED_MANIFEST_DIFFERENCES if base.get(k)!=trial.get(k))}

def phase_bounds(rows,name):
    phases=[r for r in rows if r.get('kind')=='phase']
    start=next((r for r in phases if r.get('phase')==name),None)
    if not start:return None,None
    end=next((r for r in phases if r['tick']>start['tick']),None)
    if end is None:end=next((r for r in rows if r.get('kind')=='done' and r['tick']>start['tick']),None)
    return start,end

def read_json(path):return json.loads(path.read_text()) if path.exists() else None

def presentation_summary(presents,start_gf,end_gf):
    indexed=[(i,p) for i,p in enumerate(presents) if start_gf<=p['gf']<end_gf]
    if len(indexed)<2:return None,[p for _,p in indexed]
    selected=[p for _,p in indexed]
    intervals=[(b['t']-a['t'])*1000 for a,b in zip(selected,selected[1:])]
    valid=all(math.isfinite(x) and x>0 for x in intervals)
    mismatch=sum(abs((b['t']-a['t'])-b['dt'])>0.000003 for a,b in zip(selected,selected[1:]))
    duration=selected[-1]['t']-selected[0]['t']
    def boundary(index):
        current=presents[index];previous=presents[index-1] if index>0 else None
        measured=(current['t']-previous['t'])*1000 if previous else None
        return {'ending_gf':current['gf'],'previous_gf':previous['gf'] if previous else None,
                'logged_dt_ms':current['dt']*1000,'measured_dt_ms':measured,
                'logged_dt_consistent':abs(measured-current['dt']*1000)<=.003 if measured is not None else None,
                'over_16_7_ms':current['dt']*1000>16.7,'over_33_ms':current['dt']*1000>33,'over_100_ms':current['dt']*1000>100,
                'interval_fully_inside_segment':bool(previous and start_gf<=previous['gf']<end_gf and start_gf<=current['gf']<end_gf)}
    entry=boundary(indexed[0][0]);last_index=indexed[-1][0]
    exit_boundary=boundary(last_index+1) if last_index+1<len(presents) else None
    result={'completed_swap_records':len(selected),'measured_seconds':duration,'completed_swap_hz':(len(selected)-1)/duration if duration>0 else None,
            'interval_ms':distribution(intervals) if valid else None,'over_16_7_ms':sum(x>16.7 for x in intervals),'over_33_ms':sum(x>33 for x in intervals),'over_100_ms':sum(x>100 for x in intervals),
            'logged_dt_mismatches':mismatch,'physical_scanout_verified':False,'entering_boundary':entry,'exiting_boundary':exit_boundary,
            'boundary_scope':'Interval distribution uses adjacent pairs fully inside [start_gf,end_gf). Entry/exit intervals cross the boundary and are reported separately, including long frames; they are not silently called startup or attributed to a cause.'}
    return result,selected

def validate_native_probe(config,proof,native_text,affinity=None):
    problems=[];checks={}
    if config.get('enabled') is not False:problems.append('probe requires Enabled0')
    if config.get('native_timing_probe') is not True:problems.append('probe not explicitly requested')
    if config.get('main_thread_cpu')!=-1:problems.append('native probe must not request main affinity')
    if config.get('async_log') is not True:problems.append('native probe requires async logger accounting')
    lines=native_text.splitlines()
    installed=[i for i,l in enumerate(lines) if '[native-measure]' in l and 'installed=' in l]
    ready=[i for i,l in enumerate(lines) if '[native-measure]' in l and 'ready=' in l]
    if len(installed)!=1 or not all(kv(lines[installed[0]]).get(k)==v for k,v in {'installed':1,'enabled':0,'probe':1}.items()):problems.append('missing unique successful native installed marker')
    if len(ready)!=1 or not all(kv(lines[ready[0]]).get(k)==v for k,v in {'ready':1,'iat_only':1,'pacing':0,'affinity_policy':-1,'asyncLog':1}.items()):problems.append('missing unique successful native ready marker')
    first_present=next((i for i,l in enumerate(lines) if PRESENT.search(l)),None)
    if len(installed)==len(ready)==1 and (installed[0]>=ready[0] or first_present is not None and ready[0]>=first_present):problems.append('native marker ordering invalid')
    if re.search(r'wrapper hook installed|limiter removed|\[pipeline\] fixed Update',native_text):problems.append('non-probe pipeline marker in native measurement')
    thread_rows=[l for l in lines if '[threads]' in l]
    if len(thread_rows)>1:problems.append('multiple thread configuration rows in native probe')
    for line in thread_rows:
        values=kv(line)
        masks={k:int(v,16) for k,v in re.findall(r'\b(pinnedMask|previousMask|workerMask)=(0x[0-9A-Fa-f]+)\b',line)}
        nonbinding=all(values.get(k)==v for k,v in {'policy':-1,'affinityOk':1,'asyncLog':1}.items()) and masks=={'pinnedMask':0,'previousMask':0,'workerMask':0}
        nonbinding=nonbinding and values.get('main',0)>0 and len(ready)==1 and values.get('main')==kv(lines[ready[0]]).get('thread')
        if not nonbinding:problems.append('binding or inconsistent thread configuration in native probe')
    affinity_check=None
    if affinity is not None:
        try:
            main_tid=int(affinity['main_linux_tid']);threads=affinity['threads']
            main_mask=int(threads[str(main_tid)]['Cpus_allowed'].replace(',',''),16)
            union=0
            for row in threads.values():union|=int(row['Cpus_allowed'].replace(',',''),16)
            affinity_check={'main_mask':hex(main_mask),'observed_threads_union':hex(union),'main_covers_all_observed_allowed_cpus':main_mask==union and main_mask!=0}
            if not affinity_check['main_covers_all_observed_allowed_cpus'] or not isinstance(proof,dict) or main_tid!=proof.get('pid'):problems.append('native Linux main affinity is restricted or process identity differs')
        except (KeyError,TypeError,ValueError):problems.append('malformed native Linux affinity evidence')
    if not isinstance(proof,dict):problems.append('missing native-runtime-verification.json')
    else:
        if proof.get('schema')!='isaac-native-runtime-verification-v1' or proof.get('all_original') is not True:problems.append('invalid native runtime proof header')
        try:
            valid_identity=int(proof['pid'])>0 and int(proof['starttime_ticks'])>0
            base=int(str(proof['image_base']),0)
            valid_identity=valid_identity and 0x10000<=base<=0xffff0000 and base%0x10000==0
        except (KeyError,TypeError,ValueError):valid_identity=False
        if not valid_identity:problems.append('missing guarded runtime process identity')
        rows=proof.get('checks',[])
        if not isinstance(rows,list):rows=[]
        for name,(address,expected) in NATIVE_ORIGINAL_SITES.items():
            matches=[r for r in rows if isinstance(r,dict) and r.get('name')==name]
            valid=False
            if len(matches)==1:
                r=matches[0]
                try:valid=int(str(r['static_address']),0)==address and bytes.fromhex(r['expected_hex'])==bytes.fromhex(expected) and bytes.fromhex(r['actual_hex'])==bytes.fromhex(expected)
                except (KeyError,TypeError,ValueError):pass
            checks[name]={'static_address':hex(address),'matches_audited_original':valid}
            if not valid:problems.append('original byte proof failed: '+name)
    return {'valid':not problems,'problems':problems,'checks':checks,'linux_affinity':affinity_check,'scope':'Independent guarded process-memory snapshot proves these five original code sites at observation time. SwapBuffers IAT and logger are deliberately instrumented; this is not pristine native execution.'}

def analyze_case(case,phase='fire',min_seconds=5):
    config=read_json(case/'case-config.json')
    manifest=read_json(case/'case-manifest.json')
    fixture,errors=parse_fixture((case/'game.log').read_text(errors='replace') if (case/'game.log').exists() else '')
    native_text=(case/'native.log').read_text(errors='replace') if (case/'native.log').exists() else ''
    presents,costs,stats,workers,native_errors=parse_native(native_text)
    errors+=native_errors;rejections=[]
    start=next((r for r in fixture if r.get('kind')=='start'),None)
    done=next((r for r in fixture if r.get('kind')=='done'),None)
    ready_rows=[r for r in fixture if r.get('kind')=='ready']
    ready=ready_rows[0] if len(ready_rows)==1 else None
    if len(ready_rows)>1:rejections.append('more than one normalization ready marker')
    if ready is not None and not ready.get('players'):rejections.append('normalization ready marker lacks players')
    target=start.get('config',{}).get('normalizeFirstUpdatePosition') if start else None
    if target is not None:
        if not ready or ready.get('normalized_once') is not True or ready.get('tick')!=1:
            rejections.append('missing first-update one-time normalization proof')
        elif len(ready.get('players',[]))!=1 or ready['players'][0].get('position')!=target or ready['players'][0].get('velocity')!=[0,0]:
            rejections.append('ready position or velocity differs from configured normalization')
    if not start or not start.get('continued'):rejections.append('missing verified Continue start')
    if not done or done.get('status')!='complete':rejections.append('fixture not complete')
    if any(r.get('kind') in ('rejected','additional_start') for r in fixture):rejections.append('fixture rejected or unexpected restart')
    if not config:rejections.append('missing case-config.json')
    if not manifest:rejections.append('missing full case-manifest.json')
    elif 'game/isaac-ng.exe' not in manifest:rejections.append('manifest lacks executable hash')
    if errors:rejections.append('runtime errors present')
    if any(w.get('dropped',0)!=0 or w.get('truncated',0)!=0 for w in workers):rejections.append('async log dropped or truncated records')
    all_windows=[r for r in fixture if r.get('kind')=='window']
    census_modes=[census_mode(r) for r in all_windows]
    if 'invalid' in census_modes:rejections.append('malformed census: numeric entities/effects or explicit measured=false with reason required')
    measured_windows=[r for r in all_windows if census_mode(r)=='measured']
    omitted_windows=[r for r in all_windows if census_mode(r)=='not_measured']
    census_coverage={'measured_windows':len(measured_windows),'unmeasured_windows':len(omitted_windows),
        'status':'mixed' if measured_windows and omitted_windows else 'measured' if measured_windows else 'not_measured' if omitted_windows else 'no_valid_census',
        'omission_reasons':sorted({r['census']['reason'] for r in omitted_windows})}

    marked=bool(start and any(p.get('marked') for p in start.get('players',[])))
    public_phase=[]
    for name in ('settle','fire','drain'):
        b,e=phase_bounds(fixture,name)
        if not b or not e:continue
        def birth_at(boundary):
            found=[r for r in all_windows if r['wall_ms']<=boundary['wall_ms']]
            return found[-1].get('shot_births_total',0) if found else 0
        seconds=(e['wall_ms']-b['wall_ms'])/1000
        public_phase.append({'phase':name,'commanded_input':'shoot right' if name=='fire' else 'released','wall_seconds':seconds,'game_frames':e['gf']-b['gf'],'canonical_render_callbacks':e['r']-b['r'],'observed_shot_births':birth_at(e)-birth_at(b)})
    begin,end=phase_bounds(fixture,phase)
    native=config is not None and not config.get('enabled',True)
    probe_requested=bool(config and config.get('native_timing_probe'))
    probe_proof=None;use_present=not native
    measurement_mode='candidate' if not native else 'uninstrumented-native'
    if probe_requested:
        probe_proof=validate_native_probe(config,read_json(case/'native-runtime-verification.json'),native_text,read_json(case/'thread-affinity.json'))
        if not probe_proof['valid']:rejections.extend(probe_proof['problems'])
        measurement_mode='instrumented-native-baseline' if probe_proof['valid'] else 'invalid-native-probe'
        use_present=probe_proof['valid']
        if not workers:rejections.append('missing native probe worker loss accounting')
    elif native and (presents or '[native-measure]' in native_text):
        rejections.append('stray presentation or probe records in unprobed Enabled0 case')
    public={'case':case.name,'config':config,'measurement_mode':measurement_mode,'native_runtime_verification':probe_proof,'phase':phase,'fixture_status':done.get('status') if done else None,'census_coverage':census_coverage,'errors':errors,'rejections':rejections,'dll_sha256':manifest.get('game/winmm.dll') if manifest else None,
            'log_worker':{'samples':len(workers),'max_dropped':max((w.get('dropped',0) for w in workers),default=None),'max_truncated':max((w.get('truncated',0) for w in workers),default=None),'peak_accepted_minus_written':max((w.get('accepted',0)-w.get('written',0) for w in workers),default=None)},
            'actual_presentation':None,'whole_active_presentation':None,'render_cost_windows':None,'canonical_callback_cadence':None,
            'ready_state':ready,'original_start_positions':[p.get('position') for p in start.get('players',[])] if start else None,
            'phases':public_phase,'marked_present':marked,'workload_note':'Phase names denote commanded input only. Marked can continue shooting after release; drain is not a no-load recovery segment.',
            'whole_run':{'shot_births':done.get('shot_births') if done else None,'peak_census_entities':max((r['census']['entities'] for r in measured_windows),default=None)}}
    if not begin or not end:rejections.append('missing phase boundaries');return public,manifest,start
    wall_seconds=(end['wall_ms']-begin['wall_ms'])/1000
    if wall_seconds<min_seconds:rejections.append('phase too short')
    public['segment']={'start_gf':begin['gf'],'end_gf_exclusive':end['gf'],'simulation_ticks':end['tick']-begin['tick'],'wall_seconds':wall_seconds}
    public['canonical_callback_cadence']={f'{key}_hz':(end[key]-begin[key])/wall_seconds for key in ('u','r','ui','gf') if key in begin and key in end and wall_seconds>0}
    windows=[r for r in fixture if r.get('kind')=='window' and begin['wall_ms']<=r['wall_ms']-r.get('window_ms',0) and r['wall_ms']<=end['wall_ms']]
    public['fixture_windows']={'count':len(windows),'entities':distribution([r['census']['entities'] for r in windows if census_mode(r)=='measured']),'effects':distribution([r['census']['effects'] for r in windows if census_mode(r)=='measured']),'shot_births_end':max((r.get('shot_births_total',0) for r in windows),default=None),'native_interval_brackets':[r.get('init_brackets') for r in windows]}
    if not use_present:
        public['actual_presentation_limitation']='No validated native probe timestamps are accepted. Unprobed Enabled0 only provides MC_POST_RENDER callback cadence, not display FPS. A requested probe must pass markers and independent original-byte proof before its presentation data is used.'
    else:
        summary,selected=presentation_summary(presents,begin['gf'],end['gf'])
        public['actual_presentation']=summary
        if ready and done and 'gf' in ready:
            active_summary,_=presentation_summary(presents,ready['gf'],done['gf'])
            public['whole_active_presentation']={'start_gf':ready['gf'],'end_gf_exclusive':done['gf'],'scope':'After verified first-update ready through DONE; includes settle, first-shot boundary, sustained fire and post-release Marked firing.','metrics':active_summary}
            if active_summary is None:rejections.append('missing whole-active presentation telemetry')
        else:
            public['whole_active_presentation_limitation']='No verified ready marker; a whole-active presentation interval is not inferred.'
        for item in [summary,public.get('whole_active_presentation',{}).get('metrics') if public.get('whole_active_presentation') else None]:
            if item is None:continue
            if item['interval_ms'] is None and 'nonpositive or nonfinite presentation time' not in rejections:rejections.append('nonpositive or nonfinite presentation time')
            inconsistent_boundary=any(item[k] and item[k]['logged_dt_consistent'] is False for k in ('entering_boundary','exiting_boundary'))
            if (item['logged_dt_mismatches'] or inconsistent_boundary) and 'presentation records missing or inconsistent dt' not in rejections:rejections.append('presentation records missing or inconsistent dt')
        if summary is None:rejections.append('missing actual presentation telemetry')
        else:
            full=[r for r in costs if r['end_t'] is not None and r.get('windowMs',0)>0 and selected[0]['t']<=r['end_t']-r['windowMs']/1000 and r['end_t']<=selected[-1]['t']]
            seconds=sum(r['windowMs'] for r in full)/1000
            public['render_cost_windows']={'full_windows':len(full),'total_seconds':seconds,'hidden_calls_hz':sum(r.get('hiddenCalls',0) for r in full)/seconds if seconds else None,'present_calls_hz':sum(r.get('presentCalls',0) for r in full)/seconds if seconds else None,
                'per_window_ms':{k:distribution([r[k] for r in full if k in r]) for k in ('hiddenMs','hiddenMaxMs','presentWorkMs','presentWorkMaxMs','swapWaitMs','gcMs')},
                'average_per_call_within_window_ms':{k:distribution([r[k]/r[count] for r in full if r.get(count,0)>0]) for k,count in [('hiddenMs','hiddenCalls'),('presentWorkMs','presentCalls')]},'limitation':'These are distributions of one-second aggregate costs or window maxima, not per-call p99 and not CPU-only durations.'}
            fullstats=[r for r in stats if r['end_t'] is not None and selected[0]['t']<=r['end_t']<=selected[-1]['t']]
            public['stat_counter_windows']={'samples':len(fullstats),'loop_count':distribution([r['loop_count'] for r in fullstats]),'wrapper_count':distribution([r['wrapper_count'] for r in fullstats]),'limitation':'Labels /s are window counts. They are kept separate from timestamp-derived completed SwapBuffers rate; residual_render_estimate is not CPU rendering time.'}
            if config and config.get('async_log') and not workers:rejections.append('missing async log loss accounting')
    return public,manifest,start

def compare_identity(base,trial,base_ready=None,trial_ready=None):
    keys=('continued','seed','room','stage','difficulty','config','players','time_counter')
    if not base or not trial:return {'valid':False,'missing_start':True}
    if bool(base_ready)!=bool(trial_ready):return {'valid':False,'different_fields':['ready_presence']}
    if base_ready and trial_ready and (not base_ready.get('players') or not trial_ready.get('players')):return {'valid':False,'different_fields':['ready.players_missing']}
    # Only paired, explicitly observed normalization permits ignoring the native
    # Continue door lottery's original position. All other initial fields remain.
    def normalized_start(r):
        result=dict(r)
        if base_ready and trial_ready:
            result['players']=[{k:v for k,v in p.items() if k!='position'} for p in r.get('players',[])]
        return result
    b,t=normalized_start(base),normalized_start(trial)
    different=[k for k in keys if b.get(k)!=t.get(k)]
    if base_ready and trial_ready and base_ready.get('players')!=trial_ready.get('players'):different.append('ready.players')
    return {'valid':not different,'different_fields':different,'compared_fields':keys,'normalization_ready_in_both':bool(base_ready and trial_ready),'ignored_initial_player_fields':['position'] if base_ready and trial_ready else [],'ready_players_exact':base_ready.get('players')==trial_ready.get('players') if base_ready and trial_ready else None}

def main():
    p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('trials',nargs='*',type=Path);p.add_argument('--phase',default='fire');p.add_argument('--min-seconds',type=float,default=5);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    results=[analyze_case(path,a.phase,a.min_seconds) for path in [a.baseline,*a.trials]]
    comparisons=[]
    for pub,manifest,start in results[1:]:
        m=manifest_difference(results[0][1],manifest);identity=compare_identity(results[0][2],start,results[0][0].get('ready_state'),pub.get('ready_state'))
        if not m['valid']:pub['rejections'].append('immutable case manifest differs from baseline')
        if not identity['valid']:pub['rejections'].append('initial continued-run identity differs from baseline')
        comparisons.append({'baseline':results[0][0]['case'],'trial':pub['case'],'manifest':m,'initial_state':identity})
    report={'schema':'isaac-performance-evidence-v1','cases':[r[0] for r in results],'comparisons':comparisons,'evidence_valid':all(not r[0]['rejections'] for r in results),'method':{'segment':'Use the chosen HFPERF phase boundary GameFrame interval [begin.gf,end.gf). Every adjacent presentation pair must have both records inside that interval. Entry and exit logged-dt intervals are separately reported rather than silently discarded. A second summary covers every presentation after verified ready until DONE, including first-shot and post-release intervals. Lua wall_ms and native t have different origins and are never directly subtracted.','percentiles':'Linear interpolation at (N-1)*p over sorted interval values. Counts use strict >16.7, >33 and >100ms. Mean completed-swap rate is (N-1)/(last_t-first_t), never callback/loop counts.','native':'Unprobed Enabled0 only provides fixture canonical callback counts. Opt-in native_timing_probe requires installed+ready markers, no pacing/affinity request, worker loss accounting and independent original update/render/GC/limiter runtime bytes. Valid probe data is explicitly instrumented-native-baseline, not pristine native execution.','costs':'Full render-cost windows must fit inside the selected native presentation-time interval. Wall costs include scheduling/waits; per-window aggregate percentiles are not per-call CPU-time percentiles.','integrity':'Reject incomplete/restarted fixture, runtime errors, dropped/truncated async logs, missing full manifests/executable hash, unexpected immutable manifest changes, differing initial/normalized-ready run identity, or missing/inconsistent candidate presentation records. DLL/config hashes alone may differ.','scope':'Read completed logs only. These metrics describe the tested scenario; evidence_valid means data integrity, not an automatic stable90 or all-gameplay performance verdict.'}}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('VALID EVIDENCE' if report['evidence_valid'] else 'REJECTED EVIDENCE')
    return 0 if report['evidence_valid'] else 2
if __name__=='__main__':raise SystemExit(main())
