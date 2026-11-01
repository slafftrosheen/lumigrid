#!/usr/bin/env python3
"""Read-only evidence completeness check. Never grants CE or hardware approval.

PASS in an input record is an operator declaration. This tool checks identity,
file presence and hashes; it does not independently validate measurements.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

REQUIRED_CHECKS = (
    'firmware_builds', 'safe_boot_and_reset', 'all_16_outputs',
    'combined_outputs_and_network', 'network_loss_and_reconnect',
    'sync_shutdown_and_recovery',
    'ota_and_rollback', 'configuration_power_loss', 'thermal_endurance',
    'installation_limits_review',
)
IDENTITY_FIELDS = ('source_commit','firmware_sha256','pcb_revision','serial_number',
                   'wiring_revision','power_supply','load_profile','enclosure','antenna')
RECORD_SCHEMA = 'lumigrid-qualification-v2'

def identity_hash(identity: dict[str, Any]) -> str:
    """Bind each check to the full build, specimen and installation identity.

    This detects accidental mixing of records; it is not an attestation that
    the declared setup or measurements are truthful.
    """
    if any(not isinstance(identity.get(field), str) or not identity[field].strip()
           for field in IDENTITY_FIELDS):
        return ''
    encoded = json.dumps({field: identity[field] for field in IDENTITY_FIELDS},
                         sort_keys=True, separators=(',', ':'), ensure_ascii=False)
    return hashlib.sha256(encoded.encode('utf-8')).hexdigest()

def file_hash(path: Path) -> str:
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()

def evaluate(document: Any, evidence_root: Path, firmware: Path,
             actual_commit: str, dirty: bool) -> dict[str, Any]:
    root=evidence_root.resolve()
    problems: list[str]=[]
    report: dict[str, Any]={
        'schema':'lumigrid-evidence-preflight-v2',
        'policy_revision':'2026-11-01-bound-evidence',
        'evidence_complete':False,
        'conformity_assessment':'NOT_PERFORMED_BY_TOOL',
        'measurement_verification':'OPERATOR_DECLARATIONS_NOT_INDEPENDENTLY_VERIFIED',
        'source_commit':actual_commit,'source_dirty':dirty,
        'blockers':problems,'evidence':[],
    }
    if not isinstance(document,dict):
        problems.append('Evidence document must be an object.');return report
    if document.get('schema') != RECORD_SCHEMA:
        problems.append(f'Unsupported evidence schema; expected {RECORD_SCHEMA} with declared identity and file hashes.')
    identity=document.get('identity')
    if not isinstance(identity,dict):identity={};problems.append('Identity must be an object.')
    for field in IDENTITY_FIELDS:
        if not isinstance(identity.get(field),str) or not identity[field].strip():
            problems.append(f'Missing identity: {field}.')
    fingerprint = identity_hash(identity)
    report['identity_sha256'] = fingerprint
    if not re.fullmatch(r'[0-9a-f]{40}',actual_commit or ''):problems.append('Actual source commit is invalid.')
    if identity.get('source_commit')!=actual_commit:problems.append('Source commit differs from the declared test build.')
    if dirty:problems.append('Source tree is dirty; commit and rebuild before freezing identity.')
    firmware_hash=''
    try:
        if not firmware.is_file() or firmware.stat().st_size==0:raise ValueError('missing or empty')
        firmware_hash=file_hash(firmware)
        report['firmware_sha256']=firmware_hash
        report['firmware_size']=firmware.stat().st_size
    except (OSError,ValueError) as error:problems.append(f'Firmware artifact is not readable: {error}.')
    if not firmware_hash or identity.get('firmware_sha256')!=firmware_hash:
        problems.append('Firmware hash differs from the declared test build.')
    checks=document.get('checks')
    if not isinstance(checks,list):checks=[];problems.append('Checks must be an array.')
    seen:set[str]=set();files:dict[str,dict[str,Any]]={}
    for entry in checks:
        if not isinstance(entry,dict) or not isinstance(entry.get('id'),str):
            problems.append('Invalid check record.');continue
        name=entry['id']
        if name not in REQUIRED_CHECKS:problems.append(f'Unknown check: {name}.');continue
        if name in seen:problems.append(f'Duplicate check: {name}.')
        seen.add(name)
        if entry.get('result')!='PASS':problems.append(f'{name}: result is {entry.get("result","MISSING")}.')
        if entry.get('firmware_sha256')!=firmware_hash:problems.append(f'{name}: different firmware identity.')
        if entry.get('pcb_revision')!=identity.get('pcb_revision'):problems.append(f'{name}: different PCB revision.')
        if not fingerprint or entry.get('identity_sha256') != fingerprint:
            problems.append(f'{name}: full test identity differs or is missing (build, unit, wiring, supply, loads, enclosure or antenna).')
        evidence=entry.get('evidence')
        if not isinstance(evidence,list) or not evidence:
            problems.append(f'{name}: missing evidence files.');continue
        check_paths: set[str] = set()
        for value in evidence:
            try:
                if not isinstance(value, dict):
                    raise ValueError('requires an object with path and recorded sha256')
                relative_path = value.get('path')
                declared_hash = value.get('sha256')
                if not isinstance(relative_path,str) or not relative_path or Path(relative_path).is_absolute():
                    raise ValueError('requires a relative file path')
                path=(root/relative_path).resolve()
                if not path.is_relative_to(root):raise ValueError('path escapes evidence directory')
                if not path.is_file() or path.stat().st_size==0:raise ValueError('missing, empty or not a file')
                relative=path.relative_to(root).as_posix()
                if relative in check_paths:
                    raise ValueError('duplicate evidence file within this check')
                check_paths.add(relative)
                if relative not in files:
                    files[relative]={'path':relative,'sha256':file_hash(path),'bytes':path.stat().st_size,'checks':[]}
                files[relative]['checks'].append(name)
                if not isinstance(declared_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', declared_hash):
                    problems.append(f'{name}: missing or invalid recorded SHA-256 for {relative}.')
                elif declared_hash != files[relative]['sha256']:
                    problems.append(f'{name}: evidence hash changed for {relative}.')
            except (OSError,ValueError,RuntimeError) as error:
                problems.append(f'{name}: invalid evidence path ({error}).')
    for missing in sorted(set(REQUIRED_CHECKS)-seen):problems.append(f'Missing required check: {missing}.')
    report['evidence']=[files[key] for key in sorted(files)]
    report['evidence_complete']=not problems
    return report

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--evidence-root',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        doc=json.loads(args.record.read_text(encoding='utf8'))
        commit=subprocess.check_output(['git','-C',str(args.source_root),'rev-parse','HEAD'],text=True).strip()
        dirty=bool(subprocess.check_output(['git','-C',str(args.source_root),'status','--porcelain','--untracked-files=normal'],text=True).strip())
        report=evaluate(doc,args.evidence_root or args.record.parent,args.firmware,commit,dirty)
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print(f'Preflight could not run: {error}',file=sys.stderr);return 1
    rendered=json.dumps(report,indent=2)+'\n'
    if args.output:
        # Reporting must never overwrite firmware, source evidence or an earlier
        # report. Use a new output filename for each capture.
        if args.output.exists() or args.output.is_symlink():
            print('Refusing to overwrite an existing output file.', file=sys.stderr)
            return 1
        args.output.parent.mkdir(parents=True,exist_ok=True)
        try:
            with args.output.open('x', encoding='utf8') as stream:
                stream.write(rendered)
        except OSError as error:
            print(f'Report could not be written: {error}', file=sys.stderr)
            return 1
    print(rendered,end='')
    return 0 if report['evidence_complete'] else 2

if __name__=='__main__':sys.exit(main())
