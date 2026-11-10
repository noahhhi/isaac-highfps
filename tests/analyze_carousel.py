#!/usr/bin/env python3
"""Offline carousel recordings. Never opens a process or generates input."""
import argparse, collections, hashlib, json, re, statistics, struct
from pathlib import Path
from PIL import Image
ROI=(336,176,944,400)
NUM=r'[-+\d.eE]+'
LINE=re.compile(r'\[carousel-(fixed|present)\] (.*?) poses=(.*)')
def f32(v): return struct.unpack('<f',struct.pack('<f',float(v)))[0]
def dist(v):
    v=sorted(v)
    return {'n':len(v),'min':v[0],'median':statistics.median(v),'p95':v[int((len(v)-1)*.95)],'max':v[-1]} if v else None
def sig(r): return (r['selected'],r['target'],r['count'],r['page'],r['angle'],tuple(r['poses']))
def read(case):
    events=json.loads((case/'input-events.json').read_text())
    es=events['events']; lo=es[0]['before']['tick']-2; hi=events['end']['tick']
    native=(case/'native.log').read_text(errors='replace') if (case/'native.log').exists() else ''
    fixed=[]; present=[]; dt=[]; tick=None
    for line in native.splitlines():
        m=LINE.search(line)
        if m:
            r={k:float(v) if k in ('t','a','angle') else int(v) for k,v in re.findall(r'(\w+)=('+NUM+r')',m[2])}
            r['angle']=f32(r['angle']); r['poses']=tuple(f32(v) for v in re.split('[;,]',m[3]))
            (fixed if m[1]=='fixed' else present).append(r)
        m=re.search(r'\[(?:menu|carousel)-fixed\].*?tick=(\d+)',line)
        if m: tick=int(m[1])
        m=re.search(r'\[present-frame\] t=('+NUM+r') dt=('+NUM+r')',line)
        if m and tick is not None and lo<=tick<=hi and float(m[2])>0: dt.append(float(m[2])*1000)
    rows=json.loads((case/'menu-clock.json').read_text())['samples']; by=collections.defaultdict(dict)
    for r in rows:
        if not(r.get('carousel_valid') and r.get('consistent_tick')): continue
        q={k:r['carousel_'+k] for k in ('selected','target','count','page','angle')}
        q.update(t=r['t'],tick=r['manager_tick'],ready=r['character_input_possible'],poses=tuple(v for xy,s in zip(r['carousel_positions'],r['carousel_scales']) for v in (*xy,s)))
        by[q['tick']][sig(q)]=q
    source='carousel-fixed' if fixed else 'single-valued consistent observer'
    canonical=fixed or [next(iter(v.values())) for k,v in sorted(by.items()) if len(v)==1]
    canonical=[r for r in canonical if lo<=r['tick']<=hi]
    transitions=[]
    for i,(a,b) in enumerate(zip(canonical,canonical[1:])):
        if a['selected']==b['selected']: continue
        curve=[]
        for r in canonical[i+1:]:
            if r['selected']!=b['selected']:break
            curve.append(r)
            if r['ready']:break
        transitions.append({'from':a['selected'],'to':b['selected'],'tick':b['tick'],'count':b['count'],'page':b['page'],'target':b['target'],'prior_ready':a['ready'],'curve':curve})
    short=[]
    for n,e in enumerate(es):
        if e.get('held_seconds',0)>.2:continue
        ts=[t for t in transitions if e['before']['tick']<t['tick']<=e['after']['tick']]
        short.append({'event':n,'transitions':ts})
    fb={r['tick']:r for r in fixed}; residual=[]; overs=[]; moving=0; missing=0; mismatched_context=0
    for r in present:
        if not lo<=r['tick']<=hi:continue
        a,b=fb.get(r['tick']-1),fb.get(r['tick'])
        if a is None or b is None:missing+=1;continue
        if (a['count'],a['page'])!=(b['count'],b['page']):mismatched_context+=1;continue
        moving+=a['poses']!=b['poses']
        for prev,cur,shown in zip(a['poses'],b['poses'],r['poses']):
            residual.append(abs(shown-(prev+r['a']*(cur-prev))))
            overs.append(max(min(prev,cur)-shown,shown-max(prev,cur),0))
    cap=[]
    for m in re.finditer(r'index=\(int\)(\d+), timestamp=\(guint64\)(\d+)',(case/'capture/gst.log').read_text()):
        i,ns=map(int,m.groups()); p=case/'capture'/f'frame-{i:04d}.jpg'
        with Image.open(p) as im:h=hashlib.sha256(im.convert('RGB').crop(ROI).tobytes()).hexdigest()
        cap.append({'i':i,'t':ns/1e9,'h':h})
    runs=[]
    for r in cap:
        if not runs or r['h']!=runs[-1]['h']:runs.append({'h':r['h'],'frames':[r]})
        else:runs[-1]['frames'].append(r)
    period=statistics.median([b['t']-a['t'] for a,b in zip(cap,cap[1:])])
    active=[r for r in runs if len(r['frames'])*period<=.075]
    sensitivity=[]
    for limit in (.033333334,.05,.075):
        keep={f['i'] for r in runs if len(r['frames'])*period<=limit for f in r['frames']}
        ps=[(a,b) for a,b in zip(cap,cap[1:]) if a['i'] in keep and b['i'] in keep]
        sensitivity.append({'run_limit_ms':limit*1000,'pairs':len(ps),'repeats':sum(a['h']==b['h'] for a,b in ps)})
    # Only pairs whose two frames belong to a contiguous short-run block.
    activeidx={f['i'] for r in active for f in r['frames']}
    pairs=[(a,b) for a,b in zip(cap,cap[1:]) if a['i'] in activeidx and b['i'] in activeidx]
    pub={'case':case.name,'artifacts_sha256':json.loads((case/'artifacts.json').read_text()),'canonical_source':source,'canonical_ticks':len(canonical),'observer_multivalued_ticks':sum(len(v)>1 for k,v in by.items() if lo<=k<=hi),'visible_counts':sorted(set(r['count'] for r in canonical)),
         'selection_transitions':[{'from':t['from'],'to':t['to'],'target':t['target'],'tick':t['tick'],'prior_ready':t['prior_ready'],'curve_ticks':len(t['curve'])} for t in transitions],
         'short_inputs':[{'event':s['event'],'accepted':len(s['transitions'])} for s in short],
         'actual_present_dt_ms':dist(dt),'capture_dt_ms':dist([(b['t']-a['t'])*1000 for a,b in zip(cap,cap[1:])]),
         'pixel_roi':{'bounds':ROI,'frames':len(cap),'runs':len(runs),'run_length_histogram':dict(sorted(collections.Counter(len(r['frames']) for r in runs).items())), 'short_run_threshold_sensitivity':sensitivity,'short_runs':len(active),'short_run_pairs':len(pairs),'short_run_repeat_pairs':sum(a['h']==b['h'] for a,b in pairs),'all_repeat_pairs':sum(a['h']==b['h'] for a,b in zip(cap,cap[1:])),'capture_index_gaps':sum(b['i']!=a['i']+1 for a,b in zip(cap,cap[1:]))},
         'interpolation':{'records_in_window':sum(lo<=r['tick']<=hi for r in present),'moving_records':moving,'max_lerp_residual':max(residual,default=None),'max_endpoint_overshoot':max(overs,default=None),'missing_fixed':missing,'different_context':mismatched_context,'alpha_outside_0_1':sum(not 0<=r['a']<=1 for r in present)},
         'diagnostics':[l for l in native.splitlines() if re.search(r'\[FATAL\]|\[pure-(?:lua|state)\]|\[carousel-(?:restore|state|fault|reject)\]|retaining vanilla|\[warn\].*carousel',l,re.I)]}
    pub['atomic_reversals']=[{'event':n,'key':e['key'],'schedule_seconds':e.get('schedule_seconds'),'accepted_selection_sequence':[e['before']['selected']]+[t['to'] for t in ts],'transition_tick_offsets':[t['tick']-ts[0]['tick'] for t in ts],'before_second_transition_ready':ts[1]['prior_ready'] if len(ts)>1 else None,'curves_ticks':[len(t['curve']) for t in ts]} for n,e in enumerate(es) if e.get('kind')=='atomic_reversal' for ts in [[t for t in transitions if e['before']['tick']<t['tick']<=e['after']['tick']]]]
    pub['long_holds']=[{'event':n,'from_selected':e['before']['selected'],'to_selected':e['after']['selected'],'accepted':len(ts),'step_tick_intervals':[b['tick']-a['tick'] for a,b in zip(ts,ts[1:])]} for n,e in enumerate(es) if e.get('held_seconds',0)>.2 for ts in [[t for t in transitions if e['before']['tick']<t['tick']<=e['after']['tick']]]]
    return pub,short,transitions

def compare(base,trial):
    out=[]
    for a,b in zip(base,trial):
        z={'event':a['event']};out.append(z)
        if len(a['transitions'])!=1 or len(b['transitions'])!=1:z['comparable']=False;continue
        x,y=a['transitions'][0],b['transitions'][0]
        z['comparable']=all(x[k]==y[k] for k in ('from','to','count','page','target'))
        if not z['comparable']:continue
        xs={r['tick']-x['tick']:r for r in x['curve']};ys={r['tick']-y['tick']:r for r in y['curve']}; common=sorted(xs.keys()&ys.keys())
        diffs=[abs(v-w) for k in common for v,w in zip(xs[k]['poses'],ys[k]['poses'])]
        z.update(from_selected=x['from'],to_selected=x['to'],baseline_relative_ticks=sorted(xs),trial_relative_ticks=sorted(ys),common_ticks=len(common),max_pose_error=max(diffs,default=None),different_pose_components=sum(v!=0 for v in diffs),max_angle_error=max((abs(xs[k]['angle']-ys[k]['angle']) for k in common),default=None),ready_mismatches=sum(xs[k]['ready']!=ys[k]['ready'] for k in common))
    return out

def compare_reversals(base,trial):
    out=[]
    for y in trial[0]['atomic_reversals']:
        references=[x for x in base[0]['atomic_reversals'] if x['key']==y['key'] and x['transition_tick_offsets']==y['transition_tick_offsets']]
        z={'trial_event':y['event'],'key':y['key'],'reversal_tick_offset':y['transition_tick_offsets'],'matched_baseline_event':references[0]['event'] if references else None};out.append(z)
        if not references:continue
        x=references[0]
        bs=next(s for s in base[1] if s['event']==x['event'])['transitions'];ts=next(s for s in trial[1] if s['event']==y['event'])['transitions']
        z['legs']=compare([{'event':i,'transitions':[t]} for i,t in enumerate(bs)],[{'event':i,'transitions':[t]} for i,t in enumerate(ts)])
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('cases',nargs='+',type=Path);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args(); results=[read(c) for c in args.cases]
    report={'schema':'carousel-offline-v1','cases':[x[0] for x in results],'canonical_comparisons':[{'baseline':results[0][0]['case'],'trial':x[0]['case'],'short_input_curves':compare(results[0][1],x[1]),'phase_matched_atomic_reversals':compare_reversals(results[0],x),'ordered_transition_curves':compare([{'event':i,'transitions':[t]} for i,t in enumerate(results[0][2])],[{'event':i,'transitions':[t]} for i,t in enumerate(x[2])])} for x in results[1:]],'method':{'canonical':'Candidate fixed logs are authoritative. Otherwise only single-valued observer ticks are used. Convert 9-significant-decimal logs back to binary32. Compare each matching short input selection change by relative Manager tick through first ready=1; do not equate selected ID with visible-array index or force wall alignment. Long-hold endpoints can differ because external input phase differs.','pixels':'Decode RGB, hash wheel ROI [336,176,944,400]. Report all run lengths. Conditional short-run motion statistic: runs whose frame_count*median_capture_period <=75ms; compare only adjacent frames both in these runs. Report sensitivity at 33.333334/50/75ms; the 75ms threshold includes a known 66ms settled gap between two short inputs in R1. This excludes long holds and is not an unconditional stutter or latency metric. Missing capture indices are counted.','timing':'Gst timestamps describe capture pacing. Injected present dt is restricted by latest canonical tick to first event.before.tick-2 through end.tick. Native has no injected actual-present telemetry. Input timestamps and capture clock include pipeline startup uncertainty.','interpolation':'For each present use fixed(tick-1), fixed(tick), printed alpha; check all x/y/scale values versus linear interpolation and endpoints. Printed alpha has six decimals; bounded float rounding is expected.','scope':'Only recorded isolated data. No physical control/hardware/VRR claim. Candidate observer multivalued ticks may include temporary presentation writes and are not canonical errors.'}}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
