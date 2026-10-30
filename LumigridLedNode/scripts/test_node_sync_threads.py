#!/usr/bin/env python3
"""Real host-thread interleavings around the production sync owner; not ESP32 scheduling."""
from pathlib import Path
import argparse,subprocess,tempfile,sys
CASES=['concurrent_start_admits_one','timeout_retains_entered_callback','competing_stops_cleanup_once','poll_during_100_restart_cycles','destructor_keeps_callback_alive']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sanitizer',choices=['undefined','thread'],default='undefined');a=p.parse_args()
 root=Path(__file__).resolve().parents[1];src=root/'components/sync';suite=root/'tests/node_sync_host'
 with tempfile.TemporaryDirectory(prefix='lg-sync-threads-') as d:
  binary=Path(d)/'test';cmd=['g++','-std=c++17','-O1','-g','-pthread','-DHOST_THREADED','-fsanitize='+a.sanitizer,'-fno-sanitize-recover=all','-I'+str(src/'include'),'-I'+str(suite/'include'),str(src/'NodeSync.cpp'),str(src/'ClockDiscipline.cpp'),str(suite/'thread_tests.cpp'),'-o',str(binary)]
  b=subprocess.run(cmd,text=True,capture_output=True)
  if b.returncode:print(b.stdout+b.stderr);return b.returncode
  fail=0
  for case in CASES:
   try:
    r=subprocess.run([str(binary),case],text=True,capture_output=True,timeout=10);print(r.stdout.strip() or 'FAIL '+case)
    if r.stderr:print(r.stderr.strip())
    fail+=r.returncode!=0
   except subprocess.TimeoutExpired:fail+=1;print('FAIL '+case+': timeout')
  print(f'Host-thread production-source scenarios: {len(CASES)-fail}/{len(CASES)} passed ({a.sanitizer})');return int(bool(fail))
if __name__=='__main__':sys.exit(main())
