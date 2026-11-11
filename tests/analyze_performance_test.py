#!/usr/bin/env python3
import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('performance',Path(__file__).with_name('analyze-performance.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class EvidenceTest(unittest.TestCase):
 def case(self,root,enabled=True):
  root.mkdir(exist_ok=True)
  manifest={'game/isaac-ng.exe':'exe','game/winmm.dll':'dll','game/isaac-highfps.ini':'ini','game/mods/probe/main.lua':'probe','save/gamestate1.dat':'save'}
  (root/'case-manifest.json').write_text(json.dumps(manifest));(root/'case-config.json').write_text(json.dumps({'enabled':enabled,'target_hz':90,'async_log':enabled}))
  rows=[{'kind':'start','continued':True,'gf':1,'ui':1,'tick':0,'u':0,'r':0,'wall_ms':1000000,'players':[{'marked':True}]},{'kind':'phase','phase':'fire','gf':100,'ui':200,'tick':90,'u':90,'r':180,'wall_ms':1010000},{'kind':'phase','phase':'drain','gf':130,'ui':260,'tick':120,'u':120,'r':240,'wall_ms':1011000},{'kind':'done','status':'complete','phase':'drain','gf':140,'ui':280,'tick':130,'u':130,'r':260,'wall_ms':1011333}]
  (root/'game.log').write_text('\n'.join('[HFPERF] '+json.dumps(r) for r in rows))
  lines=['[present-frame] t=49.988889 dt=0.011111 gf=99']
  for i in range(91):
   t=50+i/90;gf=100+i//3
   lines.append(f'[present-frame] t={t:.6f} dt={1/90:.6f} gf={gf}')
  lines+=['[log-worker] accepted=100 written=99 dropped=0 truncated=0']
  (root/'native.log').write_text('\n'.join(lines) if enabled else '')
  return root
 def make_probe(self,root):
  self.case(root,True)
  (root/'case-config.json').write_text(json.dumps({'enabled':False,'native_timing_probe':True,'main_thread_cpu':-1,'target_hz':90,'async_log':True}))
  proof={'schema':'isaac-native-runtime-verification-v1','pid':123,'starttime_ticks':'456','image_base':'0x400000','all_original':True,
         'checks':[{'name':name,'static_address':hex(address),'expected_hex':original,'actual_hex':original} for name,(address,original) in m.NATIVE_ORIGINAL_SITES.items()]}
  (root/'native-runtime-verification.json').write_text(json.dumps(proof))
  native=root/'native.log';native.write_text('[native-measure] installed=1 enabled=0 probe=1; instrumented vanilla, SwapBuffers IAT only\n[native-measure] ready=1 iat_only=1 pacing=0 affinity_policy=-1 thread=7 asyncLog=1\n'+native.read_text())
  return root
 def test_percentiles(self):
  d=m.distribution([1,2,3,4]);self.assertEqual(d['p50'],2.5);self.assertAlmostEqual(d['p99'],3.97)
 def test_native_not_present_fps(self):
  with tempfile.TemporaryDirectory() as d:
   p,_,_=m.analyze_case(self.case(Path(d),False),min_seconds=.1)
   self.assertEqual(p['canonical_callback_cadence']['r_hz'],60);self.assertIsNone(p['actual_presentation']);self.assertFalse(p['rejections'])
 def test_phase_and_separate_origins(self):
  with tempfile.TemporaryDirectory() as d:
   p,_,_=m.analyze_case(self.case(Path(d)),min_seconds=.1)
   self.assertFalse(p['rejections']);self.assertEqual(p['actual_presentation']['completed_swap_records'],90)
   self.assertAlmostEqual(p['actual_presentation']['completed_swap_hz'],90,places=4)
   self.assertEqual(p['actual_presentation']['over_16_7_ms'],0)
 def test_missing_present_record_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'native.log';lines=path.read_text().splitlines();del lines[20];path.write_text('\n'.join(lines))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('presentation records missing or inconsistent dt',p['rejections'])
 def test_drop_and_truncation_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'native.log';path.write_text(path.read_text().replace('dropped=0 truncated=0','dropped=1 truncated=2'))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('async log dropped or truncated records',p['rejections'])
 def test_errors_and_failed_fixture(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'game.log';path.write_text(path.read_text().replace('"complete"','"wall_timeout"')+'\nError in "PostRender" call: broken\n')
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('fixture not complete',p['rejections']);self.assertIn('runtime errors present',p['rejections'])
 def test_manifest_scope(self):
  base={'game/winmm.dll':'a','game/isaac-highfps.ini':'a','mod.lua':'same'};trial=dict(base,**{'game/winmm.dll':'b','game/isaac-highfps.ini':'b'})
  self.assertTrue(m.manifest_difference(base,trial)['valid']);trial['mod.lua']='changed';self.assertFalse(m.manifest_difference(base,trial)['valid']);self.assertFalse(m.manifest_difference(base,None)['valid'])
 def test_identity_not_arbitrarily_normalized(self):
  a={'players':[{'collectibles':[1,2]}],'seed':1};b={'players':[{'collectibles':[1,3]}],'seed':1}
  self.assertFalse(m.compare_identity(a,b)['valid']);self.assertEqual(m.compare_identity(a,b)['different_fields'],['players'])
 def test_normalized_ready_requires_both_and_preserves_other_fields(self):
  a={'seed':1,'players':[{'position':[80,280],'velocity':[0,0],'collectibles':[1,2]}]}
  b={'seed':1,'players':[{'position':[560,280],'velocity':[0,0],'collectibles':[1,2]}]}
  ready={'players':a['players']}
  self.assertFalse(m.compare_identity(a,b)['valid'])
  self.assertTrue(m.compare_identity(a,b,ready,ready)['valid'])
  self.assertFalse(m.compare_identity(a,b,ready,None)['valid'])
  self.assertFalse(m.compare_identity(a,b,ready,{'players':b['players']})['valid'])
  b['players'][0]['collectibles']=[1,3]
  self.assertFalse(m.compare_identity(a,b,ready,ready)['valid'])
 def test_configured_normalization_must_have_first_update_proof(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'game.log';rows=[json.loads(l.split('[HFPERF] ',1)[1]) for l in path.read_text().splitlines()];rows[0]['config']={'normalizeFirstUpdatePosition':[80,280]}
   path.write_text('\n'.join('[HFPERF] '+json.dumps(r) for r in rows))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('missing first-update one-time normalization proof',p['rejections'])
   rows.insert(1,{'kind':'ready','gf':2,'tick':1,'normalized_once':True,'players':[{'position':[80,280],'velocity':[0,0]}]})
   path.write_text('\n'.join('[HFPERF] '+json.dumps(r) for r in rows))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertFalse(p['rejections'])
 def test_phase_entry_hitch_is_reported_even_if_inside_pairs_are_smooth(self):
  rows=[{'t':1.0,'dt':.01,'gf':99},{'t':1.05,'dt':.05,'gf':100},{'t':1.06,'dt':.01,'gf':100},{'t':1.07,'dt':.01,'gf':101},{'t':1.18,'dt':.11,'gf':102}]
  summary,_=m.presentation_summary(rows,100,102)
  self.assertAlmostEqual(summary['interval_ms']['max'],10)
  self.assertEqual(summary['over_33_ms'],0)
  self.assertEqual(summary['entering_boundary']['logged_dt_ms'],50)
  self.assertTrue(summary['entering_boundary']['over_33_ms'])
  self.assertFalse(summary['entering_boundary']['interval_fully_inside_segment'])
  self.assertTrue(summary['exiting_boundary']['over_100_ms'])
 def test_whole_active_includes_first_shot_hitch_and_excludes_pre_ready(self):
  rows=[{'t':0,'dt':0,'gf':0},{'t':.2,'dt':.2,'gf':1},{'t':.21,'dt':.01,'gf':1},{'t':.26,'dt':.05,'gf':100},{'t':.27,'dt':.01,'gf':101},{'t':.28,'dt':.01,'gf':102}]
  steady,_=m.presentation_summary(rows,100,102);active,_=m.presentation_summary(rows,1,102)
  self.assertEqual(steady['over_33_ms'],0);self.assertEqual(active['over_33_ms'],1)
  self.assertAlmostEqual(active['interval_ms']['max'],50)
  self.assertEqual(active['entering_boundary']['logged_dt_ms'],200)
  self.assertTrue(active['entering_boundary']['over_100_ms'])
 def test_native_probe_with_original_bytes_accepts_instrumented_timing(self):
  with tempfile.TemporaryDirectory() as d:
   p,_,_=m.analyze_case(self.make_probe(Path(d)),min_seconds=.1)
   self.assertFalse(p['rejections']);self.assertEqual(p['measurement_mode'],'instrumented-native-baseline');self.assertIsNotNone(p['actual_presentation'])
   self.assertTrue(p['native_runtime_verification']['valid'])
 def test_native_probe_requires_marker_and_independent_proof(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));(root/'native-runtime-verification.json').unlink()
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('missing native-runtime-verification.json',p['rejections']);self.assertIsNone(p['actual_presentation'])
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'native.log';path.write_text(path.read_text().replace('installed=1','installed=0'))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('missing unique successful native installed marker',p['rejections']);self.assertIsNone(p['actual_presentation'])
 def test_native_probe_cannot_self_certify_changed_limiter(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'native-runtime-verification.json';proof=json.loads(path.read_text())
   row=next(r for r in proof['checks'] if r['name']=='limiter_branch');row['actual_hex']='e9f4feffff90';row['expected_hex']=row['actual_hex'];path.write_text(json.dumps(proof))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('original byte proof failed: limiter_branch',p['rejections']);self.assertIsNone(p['actual_presentation'])
 def test_native_probe_requires_worker_accounting_and_no_affinity(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'native.log';path.write_text('\n'.join(l for l in path.read_text().splitlines() if '[log-worker]' not in l))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('missing native probe worker loss accounting',p['rejections'])
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'native.log';path.write_text(path.read_text().replace('affinity_policy=-1','affinity_policy=6'))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('missing unique successful native ready marker',p['rejections'])
 def test_unprobed_disabled_stray_presentation_is_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'case-config.json';c=json.loads(path.read_text());c['enabled']=False;path.write_text(json.dumps(c))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('stray presentation or probe records in unprobed Enabled0 case',p['rejections']);self.assertIsNone(p['actual_presentation'])
 def test_native_probe_shared_logger_row_must_be_strictly_nonbinding(self):
  row='[threads] main=7 policy=-1 affinityOk=1 pinnedMask=0x0 previousMask=0x0 asyncLog=1 workerMask=0x0\n'
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'native.log';path.write_text(path.read_text().replace('[native-measure] ready=',row+'[native-measure] ready='))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertFalse(p['rejections'])
   path.write_text(path.read_text().replace('pinnedMask=0x0','pinnedMask=0x40'))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('binding or inconsistent thread configuration in native probe',p['rejections']);self.assertIsNone(p['actual_presentation'])
 def test_native_probe_linux_mask_check(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.make_probe(Path(d));path=root/'thread-affinity.json';proof={'main_linux_tid':123,'threads':{'123':{'Cpus_allowed':'ff'},'124':{'Cpus_allowed':'ff'}}};path.write_text(json.dumps(proof))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertFalse(p['rejections'])
   proof['threads']['123']['Cpus_allowed']='40';path.write_text(json.dumps(proof))
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('native Linux main affinity is restricted or process identity differs',p['rejections'])
 def test_failed_swap_rejects_performance_evidence(self):
  with tempfile.TemporaryDirectory() as d:
   root=self.case(Path(d));path=root/'native.log';path.write_text(path.read_text()+'\n[present-error] SwapBuffers failed\n')
   p,_,_=m.analyze_case(root,min_seconds=.1);self.assertIn('runtime errors present',p['rejections'])
 def with_windows(self,root,census):
  path=root/'game.log';rows=[json.loads(l.split('[HFPERF] ',1)[1])for l in path.read_text().splitlines()]
  for gf,wall,births in [(100,1010000,10),(115,1010500,30),(130,1011000,50)]:
   rows.append({'kind':'window','gf':gf,'wall_ms':wall,'window_ms':500,'census':census,'shot_births_total':births})
  rows.sort(key=lambda r:r['wall_ms']);path.write_text('\n'.join('[HFPERF] '+json.dumps(r)for r in rows))
 def test_full_census_values_unchanged(self):
  with tempfile.TemporaryDirectory()as d:
   root=self.case(Path(d));self.with_windows(root,{'entities':123,'effects':45})
   p,_,_=m.analyze_case(root,min_seconds=.1)
   self.assertFalse(p['rejections']);self.assertEqual(p['whole_run']['peak_census_entities'],123)
   self.assertEqual(p['fixture_windows']['entities']['mean'],123)
   self.assertEqual(p['fixture_windows']['effects']['mean'],45)
   self.assertEqual(p['census_coverage']['status'],'measured')
 def test_light_census_null_not_fake_zero_and_births_retained(self):
  with tempfile.TemporaryDirectory()as d:
   root=self.case(Path(d));self.with_windows(root,{'measured':False,'reason':'light_counter_profile_no_entity_census'})
   p,_,_=m.analyze_case(root,min_seconds=.1)
   self.assertFalse(p['rejections']);self.assertIsNone(p['whole_run']['peak_census_entities'])
   self.assertIsNone(p['fixture_windows']['entities']);self.assertIsNone(p['fixture_windows']['effects'])
   self.assertEqual(p['fixture_windows']['shot_births_end'],50)
   self.assertEqual(p['phases'][0]['observed_shot_births'],40)
   self.assertEqual(p['census_coverage']['status'],'not_measured')
 def test_malformed_census_never_silently_accepted(self):
  for census in ({},{'measured':False},{'measured':False,'reason':''},{'entities':2},
                 {'entities':False,'effects':0},{'entities':'2','effects':0},
                 {'measured':False,'reason':'light','entities':0,'effects':0}):
   with tempfile.TemporaryDirectory()as d:
    root=self.case(Path(d));self.with_windows(root,census)
    p,_,_=m.analyze_case(root,min_seconds=.1)
    self.assertTrue(any('malformed census' in r for r in p['rejections']),str(census))
if __name__=='__main__':unittest.main()
