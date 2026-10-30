const {test} = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const api = require(path.join(process.env.LG_COMPILED, 'sync.cjs'));
const policy = require(path.join(process.env.LG_COMPILED, 'syncPolicy.cjs'));
function status(role='worker',overrides={}) {
 return {protocolVersion:6,syncProtocol:6,syncEnabled:true,groupLabel:'Show',
   configuredRole:role,activeRole:role,running:true,clockLocked:role==='worker',clockHasEpoch:false,
   masterAddress:role==='worker'?'10.0.0.1':null,peerCount:0,lastPacketAgeMs:role==='worker'?100:null,
   packetsReceived:role==='worker'?5:0,packetsMissed:0,clockOffsetMs:0,clockJitterUs:0,
   transport:'scene',transportApplyOk:true,transportError:'none',remoteBpm:120,remoteTimelineEnabled:false,peers:[],...overrides};
}
function pair(overrides={}) { return [{ip:'10.0.0.1',status:status('master')},{ip:'10.0.0.2',status:status('worker',overrides)}]; }
function manifest(overrides={}) { return {schema:2,schemaVersion:2,protocol:1,syncProtocol:6,showHash:11,playbackHash:11,scheduleHash:22,
 scenes:[{index:0,name:'Test',checksum:4,motionChecksum:0}],timeline:{checksum:0},cueLists:[],schedules:{checksum:9},...overrides}; }

test('healthy followers can follow a leader without an epoch clock',()=>assert.equal(api.evaluateClusterTopology(pair()).isReady,true));
test('stopped master is not treated as a working leader',()=>assert.equal(api.evaluateClusterTopology([{ip:'a',status:status('master',{running:false})}]).isReady,false));
test('disabled master does not authorize group playback',()=>assert.equal(api.evaluateClusterTopology([{ip:'a',status:status('master',{syncEnabled:false})}]).isReady,false));
test('freshness is checked even if firmware says clock locked',()=>assert.equal(api.evaluateClusterTopology(pair({lastPacketAgeMs:3001})).isReady,false));
test('missing packet age is not treated as a fresh sample',()=>assert.equal(api.evaluateClusterTopology(pair({lastPacketAgeMs:null})).isReady,false));
test('negative packet age cannot pass freshness check',()=>assert.equal(api.evaluateClusterTopology(pair({lastPacketAgeMs:-1})).isReady,false));
test('follower must explicitly identify the leader',()=>assert.equal(api.evaluateClusterTopology(pair({masterAddress:null})).isReady,false));
test('wrong tracked leader is explained as disagreement',()=>assert.equal(api.evaluateClusterTopology(pair({masterAddress:'10.0.0.9'})).state,'LEADER_DISAGREEMENT'));
test('unknown transport apply result does not unlock controls',()=>assert.equal(api.evaluateClusterTopology(pair({transportApplyOk:undefined})).isReady,false));
test('unlocked follower is not ready',()=>assert.equal(api.evaluateClusterTopology(pair({clockLocked:false})).isReady,false));
test('different UI groups cannot form a single ready cluster',()=>assert.equal(api.evaluateClusterTopology(pair({groupLabel:'Other'})).isReady,false));
test('unsupported wire protocol blocks readiness',()=>assert.equal(api.evaluateClusterTopology(pair({syncProtocol:5,protocolVersion:5})).isReady,false));
test('missing protocol fields are unknown, not compatible',()=>assert.equal(api.evaluateClusterTopology(pair({syncProtocol:undefined,protocolVersion:undefined})).isReady,false));
test('conflicting protocol aliases block readiness',()=>assert.equal(api.evaluateClusterTopology(pair({syncProtocol:5,protocolVersion:6})).isReady,false));
test('one unreachable group member blocks readiness',()=>assert.equal(api.evaluateClusterTopology([...pair(),{ip:'10.0.0.3'}]).isReady,false));
test('duplicate node address cannot satisfy readiness',()=>assert.equal(api.evaluateClusterTopology([...pair(),pair()[1]]).isReady,false));
test('multiple working leaders are not silently selected',()=>assert.equal(api.evaluateClusterTopology([...pair(),{ip:'10.0.0.3',status:status('master')}]).state,'SPLIT_BRAIN'));
test('exact 3 second sample boundary remains consistent with firmware',()=>assert.equal(api.evaluateClusterTopology(pair({lastPacketAgeMs:3000})).isReady,true));
for (const name of ['compareShowManifest','comparePlaybackManifest','compareScheduleManifest']) {
 test(name+' rejects different sync versions',()=>assert.equal(api[name](manifest(),manifest({syncProtocol:5})).status,'INCOMPATIBLE'));
 test(name+' rejects missing version evidence',()=>assert.equal(api[name](manifest(),manifest({syncProtocol:undefined})).status,'UNAVAILABLE'));
}
test('equal aggregate hash cannot conceal an actual scene difference',()=>{
 const target=manifest({scenes:[{index:0,checksum:7,motionChecksum:0}]});
 assert.equal(api.comparePlaybackManifest(manifest(),target).status,'DRIFT');
});
test('full-show comparison also notices aggregate content drift',()=>assert.equal(api.compareFullShowManifest(manifest(),manifest({playbackHash:12})).status,'DRIFT'));
test('malformed manifest is not accepted or thrown into UI',()=>{
 assert.equal(api.comparePlaybackManifest(manifest(),manifest({scenes:null})).status,'UNAVAILABLE');
});
test('duplicate scene slots cannot be collapsed into a false match',()=>{
 assert.equal(api.comparePlaybackManifest(manifest(),manifest({scenes:[{index:0,checksum:1},{index:0,checksum:4}]})).status,'UNAVAILABLE');
});
test('playback comparison excludes local schedule differences',()=>{
 assert.equal(api.comparePlaybackManifest(manifest(),manifest({scheduleHash:23,schedules:{checksum:10}})).status,'MATCHED');
});
test('schedule differences remain visible separately',()=>{
 assert.equal(api.compareScheduleManifest(manifest(),manifest({scheduleHash:23})).status,'DRIFT');
});
test('input manifests are not mutated',()=>{
 const source=manifest(), target=manifest();const before=JSON.stringify([source,target]);
 api.compareShowManifest(source,target);assert.equal(JSON.stringify([source,target]),before);
});
test('current protocol is supported and aliases must agree',()=>{
 assert.equal(api.syncProtocolCompatibility({protocolVersion:6}).state,'supported');
 assert.equal(api.syncProtocolCompatibility({syncProtocol:6}).state,'supported');
 assert.equal(api.syncProtocolCompatibility({syncProtocol:5,protocolVersion:6}).state,'conflicting');
 assert.equal(api.syncProtocolCompatibility({}).state,'unknown');
});
test('readiness requires all selected nodes and matching playback evidence',()=>{
 const members=pair().map(n=>({...n,online:true,playbackStatus:'MATCHED'}));
 assert.equal(api.evaluateClusterReadiness(members).ready,true);
 assert.equal(api.evaluateClusterReadiness([...members,{ip:'10.0.0.3',online:false}]).ready,false);
 assert.equal(api.evaluateClusterReadiness(members.map((n,i)=>i?{...n,playbackStatus:'DRIFT'}:n)).ready,false);
 assert.equal(api.evaluateClusterReadiness([]).ready,false);
});

test('offline members retain their previously observed group', () => {
  assert.equal(policy.rememberSyncGroup(undefined, 'Facade'), 'Facade');
});
test('an explicitly cleared group does not retain old membership', () => {
  assert.equal(policy.rememberSyncGroup('', 'Facade'), '');
});
test('unknown membership is included conservatively in a selected group', () => {
  assert.equal(policy.inSelectedSyncGroup(undefined, 'Facade'), true);
});
test('known other-group members do not block the selected group', () => {
  assert.equal(policy.inSelectedSyncGroup('Lobby', 'Facade'), false);
  assert.equal(policy.inSelectedSyncGroup('Facade', 'Facade'), true);
  assert.equal(policy.inSelectedSyncGroup('Lobby', 'All'), true);
});
