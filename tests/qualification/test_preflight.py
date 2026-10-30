import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import json
import io
from contextlib import redirect_stdout, redirect_stderr
from unittest.mock import patch
import unittest

ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('qualification_preflight',ROOT/'scripts/qualification_preflight.py')
if SPEC is None or SPEC.loader is None: raise RuntimeError('missing module')
mod=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)

class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        (self.root/'firmware.bin').write_bytes(b'qualification fixture binary, not firmware')
        (self.root/'scope.csv').write_text('time,value\n0,1\n')
        self.sha=hashlib.sha256((self.root/'firmware.bin').read_bytes()).hexdigest()
        self.commit='a'*40
        self.doc={'schema':'lumigrid-qualification-v1','identity':{
          'source_commit':self.commit,'firmware_sha256':self.sha,'pcb_revision':'fixture-r1',
          'serial_number':'fixture-01','wiring_revision':'fixture-w1','power_supply':'fixture-psu',
          'load_profile':'fixture-load','enclosure':'fixture-enclosure','antenna':'fixture-antenna'},
          'checks':[{'id':name,'result':'PASS','firmware_sha256':self.sha,'pcb_revision':'fixture-r1','evidence':['scope.csv']} for name in mod.REQUIRED_CHECKS]}
    def tearDown(self):self.tmp.cleanup()
    def run_check(self, doc=None, **kw):
        return mod.evaluate(doc or self.doc,self.root,self.root/'firmware.bin',self.commit,kw.get('dirty',False))
    def test_sync_worker_shutdown_record_is_required(self):
        self.doc['checks']=[entry for entry in self.doc['checks'] if entry['id']!='sync_shutdown_and_recovery']
        report=self.run_check()
        self.assertFalse(report['evidence_complete'])
        self.assertTrue(any('sync_shutdown_and_recovery' in item for item in report['blockers']))
    def test_sync_worker_template_has_no_invented_pass(self):
        template=json.loads((ROOT/'docs/qualification/evidence-template.json').read_text())
        entry=next((entry for entry in template['checks'] if entry['id']=='sync_shutdown_and_recovery'),None)
        self.assertIsNotNone(entry)
        self.assertEqual(entry['result'],'NOT_RUN')
        self.assertEqual(entry['evidence'],[])
    def test_complete_only_means_evidence_completeness(self):
        report=self.run_check();self.assertTrue(report['evidence_complete'])
        self.assertEqual(report['conformity_assessment'],'NOT_PERFORMED_BY_TOOL')
        self.assertNotIn('ce_certified',report)
    def test_not_run_is_blocked(self):
        self.doc['checks'][0]['result']='NOT_RUN';self.assertFalse(self.run_check()['evidence_complete'])
    def test_failed_measurement_is_blocked(self):
        self.doc['checks'][0]['result']='FAIL';self.assertFalse(self.run_check()['evidence_complete'])
    def test_missing_check_is_blocked(self):
        self.doc['checks'].pop();self.assertFalse(self.run_check()['evidence_complete'])
    def test_duplicate_check_is_rejected(self):
        self.doc['checks'].append(copy.deepcopy(self.doc['checks'][0]));self.assertFalse(self.run_check()['evidence_complete'])
    def test_empty_evidence_is_blocked(self):
        (self.root/'scope.csv').write_bytes(b'');self.assertFalse(self.run_check()['evidence_complete'])
    def test_missing_file_is_blocked(self):
        (self.root/'scope.csv').unlink();self.assertFalse(self.run_check()['evidence_complete'])
    def test_directory_is_not_measurement(self):
        self.doc['checks'][0]['evidence']=['.'];self.assertFalse(self.run_check()['evidence_complete'])
    def test_traversal_is_blocked(self):
        self.doc['checks'][0]['evidence']=['../outside'];self.assertFalse(self.run_check()['evidence_complete'])
    def test_absolute_path_is_blocked(self):
        self.doc['checks'][0]['evidence']=[str(self.root/'scope.csv')];self.assertFalse(self.run_check()['evidence_complete'])
    def test_symlink_escape_is_blocked(self):
        (self.root/'escape').symlink_to('/etc/hostname');self.doc['checks'][0]['evidence']=['escape'];self.assertFalse(self.run_check()['evidence_complete'])
    def test_wrong_firmware_identity_is_blocked(self):
        self.doc['identity']['firmware_sha256']='b'*64;self.assertFalse(self.run_check()['evidence_complete'])
    def test_check_from_other_firmware_is_blocked(self):
        self.doc['checks'][0]['firmware_sha256']='b'*64;self.assertFalse(self.run_check()['evidence_complete'])
    def test_check_from_other_board_is_blocked(self):
        self.doc['checks'][0]['pcb_revision']='other';self.assertFalse(self.run_check()['evidence_complete'])
    def test_source_commit_mismatch_is_blocked(self):
        self.doc['identity']['source_commit']='b'*40;self.assertFalse(self.run_check()['evidence_complete'])
    def test_dirty_source_is_blocked(self):self.assertFalse(self.run_check(dirty=True)['evidence_complete'])
    def test_missing_setup_identity_is_blocked(self):
        self.doc['identity']['power_supply']='';self.assertFalse(self.run_check()['evidence_complete'])
    def test_unknown_schema_is_blocked(self):
        self.doc['schema']='v0';self.assertFalse(self.run_check()['evidence_complete'])
    def test_unknown_check_is_not_accepted(self):
        self.doc['checks'].append({'id':'made-up','result':'PASS'});self.assertFalse(self.run_check()['evidence_complete'])
    def test_file_hashes_are_recorded(self):
        report=self.run_check();self.assertEqual(report['evidence'][0]['sha256'],hashlib.sha256((self.root/'scope.csv').read_bytes()).hexdigest())
    def test_input_not_mutated(self):
        before=copy.deepcopy(self.doc);self.run_check();self.assertEqual(self.doc,before)
    def test_malformed_input_fails_closed(self):
        for doc in [None,[],{}, {'schema':'lumigrid-qualification-v1','identity':{},'checks':None}]:
            self.assertFalse(mod.evaluate(doc,self.root,self.root/'firmware.bin',self.commit,False)['evidence_complete'])

    def test_cli_refuses_overwriting_firmware_as_report(self):
        record=self.root/'record.json';record.write_text(json.dumps(self.doc))
        firmware=self.root/'firmware.bin';before=firmware.read_bytes()
        with patch('sys.argv',['preflight','--record',str(record),'--firmware',str(firmware),'--source-root',str(self.root),'--output',str(firmware)]), patch.object(mod.subprocess,'check_output',side_effect=[self.commit+'\n','']), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(mod.main(),1)
        self.assertEqual(firmware.read_bytes(),before)
    def test_cli_writes_blocked_report_without_mutating_evidence(self):
        self.doc['checks'][0]['result']='NOT_RUN'
        record=self.root/'record.json';record.write_text(json.dumps(self.doc));before=record.read_bytes()
        output=self.root/'preflight.json'
        with patch('sys.argv',['preflight','--record',str(record),'--firmware',str(self.root/'firmware.bin'),'--source-root',str(self.root),'--output',str(output)]), patch.object(mod.subprocess,'check_output',side_effect=[self.commit+'\n','']), redirect_stdout(io.StringIO()):
            self.assertEqual(mod.main(),2)
        report=json.loads(output.read_text())
        self.assertFalse(report['evidence_complete']);self.assertEqual(record.read_bytes(),before)
        self.assertEqual(report['conformity_assessment'],'NOT_PERFORMED_BY_TOOL')

if __name__=='__main__':unittest.main()
