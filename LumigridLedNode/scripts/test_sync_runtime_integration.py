#!/usr/bin/env python3
"""Compile the exact runtime callback edit with production NodeSync/ClockDiscipline.
The outer runtime, config API and service guard are host boundaries; this is not
compilation of the entire runtime translation unit.
"""
from pathlib import Path
import argparse,json,subprocess,tempfile,sys
CASES=['disable_timeout_propagates','role_restart_timeout_retains_owner','token_restart_timeout_retains_owner',
 'disable_success_releases_owner','restart_success_creates_once','restart_allocation_failure_is_reported',
 'unchanged_config_is_noop','safe_mode_does_not_start_sync','service_reconcile_lock_held','retry_after_timeout_completes']
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--before',action='store_true');args=parser.parse_args()
 root=Path(__file__).resolve().parents[1];suite=root/'tests/node_sync_host';src=root/'components/sync'
 record=json.loads((suite/'runtime_callback.json').read_text())
 text=(suite/'runtime_tests.cpp.in').read_text().replace('@@CALLBACK@@',record['old' if args.before else 'new'])
 with tempfile.TemporaryDirectory(prefix='lg-sync-runtime-') as temp:
  path=Path(temp);(path/'test.cpp').write_text(text);binary=path/'test'
  command=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Wno-unused-variable','-Wno-misleading-indentation',
   '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(src/'include'),'-I'+str(suite/'include'),
   str(src/'NodeSync.cpp'),str(src/'ClockDiscipline.cpp'),str(path/'test.cpp'),'-o',str(binary)]
  result=subprocess.run(command,capture_output=True,text=True)
  if result.returncode:print(result.stdout+result.stderr);return result.returncode
  failed=0
  for case in CASES:
   try:
    result=subprocess.run([str(binary),case],capture_output=True,text=True,timeout=5)
    print(result.stdout.strip() or 'FAIL '+case)
    if result.stderr:print(result.stderr.strip())
    failed+=result.returncode!=0
   except subprocess.TimeoutExpired:failed+=1;print('FAIL '+case+': timeout')
  print(f'Exact runtime callback host scenarios: {len(CASES)-failed}/{len(CASES)} passed');return int(bool(failed))
if __name__=='__main__':sys.exit(main())
