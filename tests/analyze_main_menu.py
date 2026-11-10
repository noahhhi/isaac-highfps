#!/usr/bin/env python3
"""Read completed menu recordings only; no process or input access."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import statistics

NUMBER = r"[-+\d.]+"
FIXED = re.compile(rf"\[menu-fixed\] tick=(\d+) page=(\d+) sliding=(\d+) xy=\(({NUMBER}),({NUMBER})\)")
PRESENT = re.compile(rf"\[menu-present\] t=({NUMBER}) tick=(\d+) page=(\d+) a=({NUMBER}) prev=\(({NUMBER}),({NUMBER})\) cur=\(({NUMBER}),({NUMBER})\) xy=\(({NUMBER}),({NUMBER})\)")


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {"count": len(values), "min": values[0], "median": statistics.median(values),
            "p95": values[int((len(values)-1)*.95)], "max": values[-1]}


def read(case):
    input_data = json.loads((case / 'input-events.json').read_text())
    raw_native = (case / 'native.log').read_bytes() if (case / 'native.log').exists() else b''
    native = raw_native.decode(errors='replace')
    # Saved byte offset is captured approximately .7 sec after capture launch.
    after = raw_native[input_data.get('native_offset', 0):].decode(errors='replace')
    samples = json.loads((case / 'menu-clock.json').read_text())['samples']
    samples = [r for r in samples if r.get('menu_initialized') and r.get('consistent_tick')]
    fixed = [{'tick': int(m[1]), 'page': int(m[2]), 'x': float(m[4]), 'y': float(m[5])}
             for m in FIXED.finditer(native)]
    presents = [{'t': float(m[1]), 'tick': int(m[2]), 'page': int(m[3]), 'alpha': float(m[4]),
                 'prev': [float(m[5]), float(m[6])], 'cur': [float(m[7]), float(m[8])],
                 'xy': [float(m[9]), float(m[10])]} for m in PRESENT.finditer(native)]
    capture = []
    for m in re.finditer(r'index=\(int\)(\d+), timestamp=\(guint64\)(\d+)', (case/'capture/gst.log').read_text()):
        i, ns = int(m[1]), int(m[2])
        f = case/'capture'/f'frame-{i:04d}.jpg'
        capture.append({'i': i, 't': ns/1e9, 'hash': hashlib.sha256(f.read_bytes()).digest()})
    windows = []
    for i, event in enumerate(input_data['events']):
        frames = [f for f in capture if event['t']+.05 <= f['t'] <= event['t']+.5]
        pairs = list(zip(frames, frames[1:]))
        windows.append({'ordinal': i, 'key': event['key'], 'frames': len(frames),
                        'first_capture_index': frames[0]['i'] if frames else None,
                        'last_capture_index': frames[-1]['i'] if frames else None,
                        'adjacent_pairs': len(pairs), 'identical_jpeg_pairs': sum(a['hash'] == b['hash'] for a, b in pairs),
                        'repeat_pair_start_offsets_seconds': [a['t']-event['t'] for a,b in pairs if a['hash']==b['hash']],
                        'capture_frame_gaps': sum(b['i'] != a['i']+1 for a,b in pairs)})
    dt = [float(m[1])*1000 for m in re.finditer(r'\[present-frame\] t=[\d.]+ dt=([\d.]+)', after) if float(m[1]) > 0]
    capture_dt = [(b['t']-a['t'])*1000 for a,b in zip(capture, capture[1:]) if a['t'] >= .5]
    # With no interpolation hook, observer values are canonical. Multiple values
    # at a tick are retained and reported, never silently selected as canonical.
    by_tick = defaultdict(set)
    for row in samples:
        if 'viewport_x' in row and 'viewport_y' in row:
            by_tick[row['manager_tick']].add((row['current_menu'], row['viewport_x'], row['viewport_y']))
    canonical = fixed or [{'tick': k, 'page': next(iter(v))[0], 'x': next(iter(v))[1], 'y': next(iter(v))[2]}
                          for k,v in sorted(by_tick.items()) if len(v) == 1]
    transitions = []
    for a,b in zip(canonical, canonical[1:]):
        if (a['page'], b['page']) in ((3,5), (5,3)):
            transitions.append({'tick': b['tick'], 'from': a['page'], 'to': b['page'],
                                'rows': {r['tick']-b['tick']: r for r in canonical if b['tick'] <= r['tick'] <= b['tick']+60}})
    rate_rows=[]
    if transitions:
        lo,hi=transitions[0]['tick'],transitions[-1]['tick']+60
        rate_rows=[(r['t'],r['tick']) for r in presents if lo<=r['tick']<=hi] if presents else [(r['t'],r['manager_tick']) for r in samples if lo<=r['manager_tick']<=hi]
    fixed_by_tick = {r['tick']: r for r in fixed}
    residual = []; bounds = []; prev_errors = []; cur_errors = []; missing = 0
    moving_present = 0
    for r in presents:
        for axis in range(2):
            residual.append(abs(r['xy'][axis] - (r['prev'][axis] + r['alpha']*(r['cur'][axis]-r['prev'][axis]))))
            bounds.append(max(min(r['prev'][axis],r['cur'][axis])-r['xy'][axis], r['xy'][axis]-max(r['prev'][axis],r['cur'][axis]), 0))
        if r['prev'] != r['cur']: moving_present += 1
        cur, prev = fixed_by_tick.get(r['tick']), fixed_by_tick.get(r['tick']-1)
        if cur:
            cur_errors.extend(abs(r['cur'][a]-cur[k]) for a,k in enumerate(('x','y')))
        else: missing += 1
        if prev and cur and prev['page'] == cur['page']:
            prev_errors.extend(abs(r['prev'][a]-prev[k]) for a,k in enumerate(('x','y')))
    selected = []
    for row in samples:
        if row.get('current_menu') == 3 and 'main_selected' in row:
            if not selected or selected[-1] != row['main_selected']: selected.append(row['main_selected'])
    public = {'case': case.name, 'artifacts_sha256': json.loads((case/'artifacts.json').read_text()),
              'input_count': len(input_data['events']), 'input_sequence': [e['key'] for e in input_data['events']],
              'jpeg_moving_windows': windows,
              'jpeg_total_pairs': sum(w['adjacent_pairs'] for w in windows),
              'jpeg_identical_pairs': sum(w['identical_jpeg_pairs'] for w in windows),
              'capture_dt_ms': distribution(capture_dt), 'actual_present_dt_ms': distribution(dt),
              'actual_present_median_hz': 1000/statistics.median(dt) if dt else None,
              'selection_observed': selected or None,
              'canonical_source': 'menu-fixed' if fixed else 'single-valued consistent-tick observer',
              'observer_multivalued_ticks': sum(len(v)>1 for v in by_tick.values()),
              'canonical_transition_count': len(transitions),
              'manager_tick_hz_regression': statistics.linear_regression([r[0] for r in rate_rows],[r[1] for r in rate_rows]).slope if len(rate_rows)>1 else None,
              'interpolation': {'present_records': len(presents), 'moving_present_records': moving_present,
                                'max_lerp_residual': max(residual, default=None), 'max_endpoint_overshoot': max(bounds, default=None),
                                'max_cur_vs_fixed_error': max(cur_errors, default=None), 'max_prev_vs_prior_same_page_fixed_error': max(prev_errors, default=None),
                                'missing_current_fixed_records': missing,
                                'alpha_outside_unit_interval': sum(not 0<=r['alpha']<=1 for r in presents)},
              'menu_restore_warning_lines': [line for line in native.splitlines() if re.search(r'\[menu[^]]*(?:restore|warn|fail)|menu.*restore.*(?:mismatch|fail|fault)', line, re.I)],
              'fatal_or_fallback_lines': [line for line in native.splitlines() if re.search(r'\[FATAL\]|retaining vanilla limiter|faulted',line,re.I)]}
    return public, transitions


def compare(base, trial):
    results=[]
    for i,(b,t) in enumerate(zip(base,trial)):
        common = sorted(b['rows'].keys() & t['rows'].keys())
        errors = [abs(b['rows'][k][axis]-t['rows'][k][axis]) for k in common for axis in ('x','y')]
        results.append({'ordinal':i,'direction':[b['from'],b['to']], 'same_direction':(b['from'],b['to'])==(t['from'],t['to']),
                        'common_relative_ticks':len(common),'baseline_missing_relative_ticks': sorted(set(range(61))-b['rows'].keys()),
                        'trial_missing_relative_ticks':sorted(set(range(61))-t['rows'].keys()),
                        'max_xy_error':max(errors,default=None),
                        'errors_above_0_000001':sum(x>1e-6 for x in errors)})
    return {'baseline_transitions':len(base),'trial_transitions':len(trial),'transitions':results}


def main():
    p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('trials',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    base, bt=read(a.baseline);trials=[read(path) for path in a.trials]
    report={'cases':[base,*[x[0] for x in trials]],'canonical_comparisons':[{'baseline':base['case'],'trial':pub['case'],**compare(bt,t)} for pub,t in trials],
            'method':{'pixel_repeat':'SHA256-equal complete JPEG bytes. Deterministic identical JPEGs prove identical captured frames; unequal files are not proof of meaningful visual motion.',
                      'windows':'For each input, select capture timestamp in [event.t+0.05,event.t+0.5], then compare adjacent selected frames. Input wall clock and capture stream start have pipeline startup uncertainty; no exact input-to-photon latency is claimed.',
                      'index_algorithm':'Parse integer index and nanosecond timestamp from each GstMultiFileSink message, convert timestamp/1e9 to seconds, keep frames satisfying the inclusive time window, then zip(selected_frames, selected_frames[1:]). Both members must be inside the window. No floor(time*nominalHz) index estimate is used. Capture index gaps are counted explicitly.',
                      'canonical':'Align each observed 3-to-5 or 5-to-3 page change to relative manager tick 0; compare x/y at matching ticks 0..60. Observer samples with multiple values at one tick are excluded. Candidate menu-fixed is authoritative and avoids temporary presentation writes.',
                      'precision':'Native observer stores exact binary32 values; menu-fixed prints six decimals. Equality tolerance is 0.000001 native viewport units.',
                      'interpolation':'Check logged xy=prev+alpha*(cur-prev), alpha range, endpoint bounds, cur matches same-tick fixed state and prev matches prior same-page fixed state. Six-decimal alpha plus float math creates bounded rounding residual.',
                      'scope':'Only completed isolated menu recordings are read. No game/input operations. Eight main-page transitions do not cover every menu animation. Arrow selection has no canonical selection stream if selection_observed is null; duplicate statistics alone cannot prove its native logic unchanged.'}}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
