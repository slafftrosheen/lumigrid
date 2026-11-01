#!/usr/bin/env python3
"""Compile production NodeSync + ClockDiscipline against deterministic host SDK boundaries.
Not an ESP-IDF build, scheduler proof or physical network test.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
import sys

CASES = [
 'timeout_reports_error','timeout_blocks_restart','late_ack_releases_ownership',
 'exit_requires_reap','stale_ack_cannot_stop_next_worker','semaphore_allocation_failure',
 'task_creation_failure','invalid_role_rejected','reentrant_start_stop_serialized',
 'completion_cannot_publish_restart_early','destructor_waits_for_ack',
 'recv_timeout_failure_rejects_socket','send_timeout_failure_rejects_socket',
 'stop_does_not_shutdown_foreign_socket','remote_cleanup_acknowledged',
 'cleanup_failure_blocks_restart','status_does_not_call_provider',
 'leader_transport_generation','follower_transport_generation','stop_clears_master_and_age',
 'packet_after_stop_not_applied','oversized_datagram_rejected','normal_datagram_still_applied',
 'self_stop_never_waits_for_itself','cleanup_reentrant_start_rejected','socket_wait_fits_manual_cadence',
 'auto_loss_releases_before_leading','auto_cleanup_failure_blocks_promotion',
 'worker_timeout_retries_remote_cleanup','worker_packet_at_zero_still_expires',
 'auto_first_election_preserves_local_transport','expired_peer_slots_recycled','live_peer_slots_preserved',
 'preferred_timeline_leader_releases','preferred_cue_leader_releases',
 'stale_leader_releases_before_reacquire','stale_changed_leader_releases',
 'rejected_leader_handover_retries','current_leader_replay_does_not_release',
]
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 default=Path(__file__).resolve().parents[1]
 parser.add_argument('--source',type=Path,default=default,help='LumigridLedNode source directory')
 parser.add_argument('--case',choices=CASES)
 args=parser.parse_args()
 suite=default/'tests/node_sync_host'
 source=args.source.resolve()/'components/sync'
 with tempfile.TemporaryDirectory(prefix='lg-node-sync-') as temp:
  binary=Path(temp)/'node-sync-tests'
  command=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Wno-unused-variable',
   '-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all',
   '-I'+str(source/'include'),'-I'+str(suite/'include'),
   str(source/'NodeSync.cpp'),str(source/'ClockDiscipline.cpp'),str(suite/'node_sync_tests.cpp'),'-o',str(binary)]
  build=subprocess.run(command,text=True,capture_output=True)
  if build.returncode:
   print(build.stdout+build.stderr);return build.returncode
  if build.stderr: print(build.stderr)
  failures=0
  cases=[args.case] if args.case else CASES
  for case in cases:
   try:
    result=subprocess.run([str(binary),case],text=True,capture_output=True,timeout=5)
    print(result.stdout.strip() or 'FAIL '+case)
    if result.stderr: print(result.stderr.strip())
    failures+=result.returncode!=0
   except subprocess.TimeoutExpired:
    failures+=1;print('FAIL '+case+': timed out')
  print(f'NodeSync production-source host scenarios: {len(cases)-failures}/{len(cases)} passed')
  return 1 if failures else 0
if __name__=='__main__': sys.exit(main())
