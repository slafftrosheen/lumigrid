#include "NodeSync.hpp"
#include <limits>
using namespace lg;
#define CHECK(value) host::check((value), #value)
struct Fixture { LightingRuntime engine; ConfigStore config; ClockDiscipline clock; NodeSync sync{engine,config,clock}; };
static void complete(Fixture& f,size_t index=0){host::run(index);CHECK(f.sync.stop(0)==ESP_OK);}
static NodeSync::SyncPacket heartbeat(){
 NodeSync::SyncPacket p{};p.magic=0x4C475359;p.version=6;p.type=NodeSync::PacketType::Heartbeat;
 p.token=1234;p.sequence=1;p.sender_mac[0]=2;p.sender_mac[5]=1;p.flags=2;
 p.global_clock_ms=1000;p.transport=1;p.active_scene=3;p.bpm=120;
 p.transport_generation=42;p.cue_generation=7;return p;
}
static int deliver(const NodeSync::SyncPacket& p,void* dst,size_t len,sockaddr* src,socklen_t* slen,size_t extra=0){
 std::vector<uint8_t> bytes(sizeof(p)+extra);std::memcpy(bytes.data(),&p,sizeof(p));
 auto* addr=reinterpret_cast<sockaddr_in*>(src);addr->sin_family=AF_INET;addr->sin_addr.s_addr=htonl(0xC0A80101);*slen=sizeof(*addr);
 size_t copied=std::min(len,bytes.size());std::memcpy(dst,bytes.data(),copied);return static_cast<int>(copied);
}
int main(int argc,char** argv){
 CHECK(argc==2);std::string name=argv[1];
 if(name=="timeout_reports_error"){
  Fixture f;CHECK(f.sync.start(NodeRole::Worker)==ESP_OK);CHECK(f.sync.stop(0)==ESP_ERR_TIMEOUT);complete(f);
 } else if(name=="timeout_blocks_restart"){
  Fixture f;CHECK(f.sync.start(NodeRole::Worker)==ESP_OK);f.sync.stop(0);CHECK(f.sync.start(NodeRole::Worker)==ESP_ERR_INVALID_STATE);complete(f);
 } else if(name=="late_ack_releases_ownership"){
  Fixture f;int cleared=0;f.sync.set_transport_callback([&](const auto&){++cleared;});
  f.sync.start(NodeRole::Worker);f.sync.stop(0);CHECK(cleared==0);complete(f);CHECK(cleared==1);CHECK(f.sync.stop(0)==ESP_OK);CHECK(cleared==1);
 } else if(name=="exit_requires_reap"){
  Fixture f;f.sync.start(NodeRole::Worker);host::run(0);CHECK(f.sync.start(NodeRole::Worker)==ESP_ERR_INVALID_STATE);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="stale_ack_cannot_stop_next_worker"){
  Fixture f;f.sync.start(NodeRole::Worker);complete(f);CHECK(f.sync.start(NodeRole::Master)==ESP_OK);CHECK(f.sync.stop(0)==ESP_ERR_TIMEOUT);complete(f,1);
 } else if(name=="semaphore_allocation_failure"){
  host::sem_failures=2;Fixture f;CHECK(f.sync.start(NodeRole::Worker)==ESP_ERR_NO_MEM);CHECK(host::create_count==0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="task_creation_failure"){
  Fixture f;host::create_fails=true;CHECK(f.sync.start(NodeRole::Worker)==ESP_ERR_NO_MEM);CHECK(!f.sync.status().running);CHECK(f.sync.stop(0)==ESP_OK);
  host::create_fails=false;CHECK(f.sync.start(NodeRole::Worker)==ESP_OK);complete(f);
 } else if(name=="invalid_role_rejected"){
  Fixture f;CHECK(f.sync.start(static_cast<NodeRole>(99))==ESP_ERR_INVALID_ARG);CHECK(host::create_count==0);
 } else if(name=="reentrant_start_stop_serialized"){
  Fixture f;int nested=ESP_OK;host::create_hook=[&]{nested=f.sync.stop(0);};f.sync.start(NodeRole::Worker);
  CHECK(nested==ESP_ERR_INVALID_STATE);host::create_hook=nullptr;complete(f);
 } else if(name=="completion_cannot_publish_restart_early"){
  Fixture f;int nested=ESP_OK;f.sync.start(NodeRole::Worker);host::give_hook=[&]{nested=f.sync.start(NodeRole::Master);};host::run(0);
  CHECK(nested==ESP_ERR_INVALID_STATE);host::give_hook=nullptr;CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="destructor_waits_for_ack"){
  LightingRuntime e;ConfigStore c;ClockDiscipline clock;auto* s=new NodeSync(e,c,clock);s->start(NodeRole::Worker);int waits=0;
  host::take_hook=[&]{if(++waits==2)host::run(0);};delete s;
  CHECK(waits==2);CHECK(host::delete_count==1);CHECK(host::give_count==1);host::take_hook=nullptr;
 } else if(name=="recv_timeout_failure_rejects_socket"){
  Fixture f;host::socket_result=10;host::fail_option=SO_RCVTIMEO;f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){f.sync.stop(0);return -1;};host::run(0);
  CHECK(host::receive_count==0);CHECK(host::close_count==1);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="send_timeout_failure_rejects_socket"){
  Fixture f;host::socket_result=10;host::fail_option=SO_SNDTIMEO;f.sync.start(NodeRole::Master);
  host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){f.sync.stop(0);return -1;};host::run(0);
  CHECK(host::send_count==0);CHECK(host::close_count==1);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="stop_does_not_shutdown_foreign_socket"){
  Fixture f;host::socket_result=10;f.sync.start(NodeRole::Worker);int error=ESP_OK;
  host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){error=f.sync.stop(0);return -1;};host::run(0);
  CHECK(error!=ESP_OK);CHECK(host::shutdown_count==0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="remote_cleanup_acknowledged"){
  Fixture f;int cleared=0;f.sync.set_remote_transport_callback([&](const auto& snap,uint8_t&){CHECK(snap.mode==ShowTransportMode::Standalone);++cleared;return true;});
  f.sync.start(NodeRole::Worker);complete(f);CHECK(cleared==1);f.sync.stop(0);CHECK(cleared==1);
 } else if(name=="cleanup_failure_blocks_restart"){
  Fixture f;bool allow=false;f.sync.set_remote_transport_callback([&](const auto&,uint8_t& error){error=4;return allow;});
  f.sync.start(NodeRole::Worker);host::run(0);CHECK(f.sync.stop(0)==ESP_FAIL);CHECK(f.sync.start(NodeRole::Master)==ESP_ERR_INVALID_STATE);
  allow=true;CHECK(f.sync.stop(0)==ESP_OK);CHECK(f.sync.start(NodeRole::Master)==ESP_OK);complete(f,1);
 } else if(name=="status_does_not_call_provider"){
  Fixture f;int calls=0;f.sync.set_transport_provider([&](uint64_t){++calls;CHECK(host::critical_depth==0);return ShowTransportSnapshot{};});
  f.sync.start(NodeRole::Master);f.sync.status();CHECK(calls==0);complete(f);
 } else if(name=="leader_transport_generation"){
  Fixture f;host::socket_result=10;f.sync.set_transport_provider([&](uint64_t){CHECK(host::critical_depth==0);ShowTransportSnapshot s{};s.mode=ShowTransportMode::Timeline;s.transport_generation=42;s.cue_generation=7;return s;});
  f.sync.start(NodeRole::Master);host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){CHECK(f.sync.status().transport_generation==42);f.sync.stop(0);return -1;};
  host::run(0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="follower_transport_generation"){
  Fixture f;host::socket_result=10;int count=0;auto p=heartbeat();f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){if(count++==0)return deliver(p,data,len,from,slen);CHECK(f.sync.status().transport_generation==42);f.sync.stop(0);return -1;};
  host::run(0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="stop_clears_master_and_age"){
  Fixture f;host::socket_result=10;int count=0;auto p=heartbeat();f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){if(count++==0)return deliver(p,data,len,from,slen);f.sync.stop(0);return -1;};
  host::run(0);CHECK(f.sync.stop(0)==ESP_OK);auto status=f.sync.status();CHECK(status.master_address==0);CHECK(status.last_packet_age_ms==UINT32_MAX);CHECK(!status.running);
 } else if(name=="packet_after_stop_not_applied"){
  Fixture f;host::socket_result=10;auto p=heartbeat();f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){f.sync.stop(0);return deliver(p,data,len,from,slen);};
  host::run(0);CHECK(f.engine.activations==0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="oversized_datagram_rejected"){
  Fixture f;host::socket_result=10;auto p=heartbeat();int count=0;f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){if(count++==0)return deliver(p,data,len,from,slen,8);f.sync.stop(0);return -1;};
  host::run(0);CHECK(f.engine.activations==0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="normal_datagram_still_applied"){
  Fixture f;host::socket_result=10;auto p=heartbeat();int count=0;f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){if(count++==0)return deliver(p,data,len,from,slen);f.sync.stop(0);return -1;};
  host::run(0);CHECK(f.engine.activations==1);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="self_stop_never_waits_for_itself"){
  Fixture f;host::socket_result=10;f.sync.start(NodeRole::Worker);
  host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){int n=host::take_count;CHECK(f.sync.stop(1500)==ESP_ERR_INVALID_STATE);CHECK(host::take_count==n);return -1;};
  host::run(0);CHECK(f.sync.stop(0)==ESP_OK);
 } else if(name=="cleanup_reentrant_start_rejected"){
  Fixture f;int nested=ESP_OK;f.sync.set_transport_callback([&](const auto&){nested=f.sync.start(NodeRole::Master);});
  f.sync.start(NodeRole::Worker);complete(f);CHECK(nested==ESP_ERR_INVALID_STATE);
 } else if(name=="socket_wait_fits_manual_cadence"){
  Fixture f;host::socket_result=10;f.sync.start(NodeRole::Master);
  host::recv_hook=[&](void*,size_t,sockaddr*,socklen_t*){f.sync.stop(0);return -1;};host::run(0);
  CHECK(host::recv_timeout_us>0&&host::recv_timeout_us<=50000);CHECK(host::send_timeout_us>0&&host::send_timeout_us<=50000);CHECK(f.sync.stop(0)==ESP_OK);
 } else {std::fprintf(stderr,"unknown test %s\n",name.c_str());return 2;}
 CHECK(host::critical_depth==0);std::printf("PASS %s\n",name.c_str());return 0;
}
