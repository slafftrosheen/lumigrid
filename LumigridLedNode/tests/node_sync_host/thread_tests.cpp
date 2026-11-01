#include "NodeSync.hpp"
using namespace lg;
#define CHECK(x) host::check((x),#x)
struct Gate {
 std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
 void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}
 void wait(){std::unique_lock<std::mutex> lock(mutex);CHECK(cv.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}));}
 void release(){std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all();}
};
int main(int argc,char**argv){
 CHECK(argc==2);std::string name=argv[1];LightingRuntime engine;ConfigStore config;ClockDiscipline clock;
 if(name=="concurrent_start_admits_one"){
  NodeSync sync(engine,config,clock);std::atomic<bool> go{false};int a=-2,b=-2;
  std::thread ta([&]{while(!go.load())std::this_thread::yield();a=sync.start(NodeRole::Worker);});
  std::thread tb([&]{while(!go.load())std::this_thread::yield();b=sync.start(NodeRole::Master);});go=true;ta.join();tb.join();
  CHECK((a==ESP_OK) != (b==ESP_OK));CHECK(host::create_count==1);CHECK(sync.stop(1000)==ESP_OK);host::join();
 } else if(name=="timeout_retains_entered_callback"){
  NodeSync sync(engine,config,clock);Gate gate;std::atomic<int> cleanup{0};
  sync.set_transport_provider([&](uint64_t){CHECK(host::critical_depth==0);gate.block();return ShowTransportSnapshot{};});
  sync.set_remote_transport_callback([&](const auto&,uint8_t&){++cleanup;return true;});
  CHECK(sync.start(NodeRole::Master)==ESP_OK);gate.wait();CHECK(sync.stop(5)==ESP_ERR_TIMEOUT);
  CHECK(cleanup==0);CHECK(sync.start(NodeRole::Worker)==ESP_ERR_INVALID_STATE);gate.release();
  CHECK(sync.stop(1000)==ESP_OK);CHECK(cleanup==1);host::join();
 } else if(name=="competing_stops_cleanup_once"){
  NodeSync sync(engine,config,clock);std::atomic<int> cleanups{0};sync.set_transport_callback([&](const auto&){++cleanups;});
  CHECK(sync.start(NodeRole::Worker)==ESP_OK);std::atomic<bool> go{false};int r[3]={-2,-2,-2};std::thread t[3];
  for(int i=0;i<3;++i)t[i]=std::thread([&,i]{while(!go.load())std::this_thread::yield();r[i]=sync.stop(1000);});
  go=true;for(auto& thread:t)thread.join();int passed=0;for(int n:r){CHECK(n==ESP_OK||n==ESP_ERR_INVALID_STATE);passed+=n==ESP_OK;}
  CHECK(passed>=1&&cleanups==1);CHECK(sync.stop(1000)==ESP_OK);host::join();
 } else if(name=="poll_during_100_restart_cycles"){
  NodeSync sync(engine,config,clock);std::atomic<bool> done{false};std::atomic<int> reads{0};
  sync.set_transport_provider([&](uint64_t){CHECK(host::critical_depth==0);ShowTransportSnapshot snap{};snap.mode=ShowTransportMode::Timeline;snap.transport_generation=42;snap.cue_generation=7;return snap;});
  std::thread reader([&]{while(!done.load()){auto status=sync.status();if(status.transport==2)CHECK(status.transport_generation==42);++reads;}});
  for(int i=0;i<100;++i){CHECK(sync.start(i%2?NodeRole::Worker:NodeRole::Master)==ESP_OK);std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(sync.stop(1000)==ESP_OK);}
  done=true;reader.join();CHECK(reads>0);CHECK(host::create_count==100);CHECK(host::shutdown_count==0);host::join();
 } else if(name=="stop_during_leader_handover_cancels_candidate"){
  NodeSync sync(engine,config,clock);Gate release;std::atomic<int> received{0},applied{0},releases{0};
  NodeSync::SyncPacket old{};old.magic=0x4C475359;old.version=6;old.type=NodeSync::PacketType::Heartbeat;
  old.token=1234;old.sequence=50;old.sender_mac[0]=2;old.sender_mac[5]=1;
  old.transport=2;old.transport_generation=42;old.global_clock_ms=1000;
  auto next=old;next.sender_mac[5]=0;next.sequence=1;
  sync.set_remote_transport_callback([&](const auto& snap,uint8_t&){
   CHECK(host::critical_depth==0);
   if(snap.mode==ShowTransportMode::Standalone){if(++releases==1)release.block();}
   else ++applied;
   return true;
  });
  host::recv_hook=[&](void* data,size_t len,sockaddr* from,socklen_t* slen){
   const int n=received++;
   if(n>1){std::this_thread::sleep_for(std::chrono::milliseconds(1));return -1;}
   CHECK(len>=sizeof(old));std::memcpy(data,n==0?&old:&next,sizeof(old));
   auto* address=reinterpret_cast<sockaddr_in*>(from);address->sin_family=AF_INET;
   address->sin_addr.s_addr=htonl(n==0?0xC0A80101:0xC0A80102);*slen=sizeof(*address);
   return static_cast<int>(sizeof(old));
  };
  CHECK(sync.start(NodeRole::Auto)==ESP_OK);release.wait();
  CHECK(applied==1&&engine.feed_count==1&&host::send_count==1);
  CHECK(sync.status().master_address==htonl(0xC0A80101));
  CHECK(sync.stop(5)==ESP_ERR_TIMEOUT);CHECK(sync.start(NodeRole::Worker)==ESP_ERR_INVALID_STATE);
  release.release();CHECK(sync.stop(1000)==ESP_OK);host::join();
  CHECK(applied==1&&engine.feed_count==1&&host::send_count==1&&releases==2);
 } else if(name=="destructor_keeps_callback_alive"){
  auto* sync=new NodeSync(engine,config,clock);Gate gate;std::atomic<bool> destroyed{false};
  sync->set_transport_provider([&](uint64_t){gate.block();return ShowTransportSnapshot{};});
  CHECK(sync->start(NodeRole::Master)==ESP_OK);gate.wait();std::thread destruction([&]{delete sync;destroyed=true;});
  std::this_thread::sleep_for(std::chrono::milliseconds(20));CHECK(!destroyed);gate.release();destruction.join();CHECK(destroyed);host::join();
 } else return 2;
 std::printf("PASS %s\n",name.c_str());return 0;
}
