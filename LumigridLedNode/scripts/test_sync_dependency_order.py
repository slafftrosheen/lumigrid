#!/usr/bin/env python3
"""Compile exact rollback method and restart-order excerpts with production sync.
SDK services outside NodeSync/ClockDiscipline are host adapters.
"""
from pathlib import Path
import argparse,json,subprocess,tempfile,sys
CASES=['rollback_preserves_live_dependencies','rollback_keeps_original_failure','scheduler_restart_waits_for_sync','scheduler_only_restart_waits_for_sync']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',action='store_true');a=p.parse_args()
 root=Path(__file__).resolve().parents[1];suite=root/'tests/node_sync_host';src=root/'components/sync';key='old' if a.before else 'new'
 text=(suite/'rollback_tests.cpp.in').read_text().replace('@@ROLLBACK@@',json.loads((suite/'rollback_method.json').read_text())[key]).replace('@@ORDER@@',json.loads((suite/'dependency_order.json').read_text())[key])
 with tempfile.TemporaryDirectory(prefix='lg-sync-deps-') as d:
  t=Path(d);(t/'test.cpp').write_text(text)
  cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Wno-unused-variable','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(src/'include'),'-I'+str(suite/'include'),str(src/'NodeSync.cpp'),str(src/'ClockDiscipline.cpp'),str(t/'test.cpp'),'-o',str(t/'test')]
  b=subprocess.run(cmd,text=True,capture_output=True)
  if b.returncode:print(b.stdout+b.stderr);return b.returncode
  fails=0
  for case in CASES:
   r=subprocess.run([str(t/'test'),case],capture_output=True,text=True,timeout=5);print(r.stdout.strip() or 'FAIL '+case)
   if r.stderr:print(r.stderr.strip())
   fails+=bool(r.returncode)
  print(f'Exact dependency-teardown host scenarios: {len(CASES)-fails}/{len(CASES)} passed');return int(bool(fails))
if __name__=='__main__':sys.exit(main())
