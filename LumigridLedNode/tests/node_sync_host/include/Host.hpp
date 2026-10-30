#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>

using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
 ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103, ESP_ERR_TIMEOUT=0x107;
using BaseType_t=int; using UBaseType_t=unsigned; using TickType_t=uint32_t;
constexpr int pdTRUE=1, pdFALSE=0, pdPASS=1;
constexpr TickType_t portMAX_DELAY=UINT32_MAX;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
struct portMUX_TYPE { int depth=0; };
#define portMUX_INITIALIZER_UNLOCKED {}
struct HostSemaphore { bool available=false; bool deleted=false; };
using SemaphoreHandle_t=HostSemaphore*;
using TaskFunction_t=void(*)(void*);
struct HostTask { TaskFunction_t fn; void* arg; bool done=false; };
using TaskHandle_t=HostTask*;
namespace host {
inline uint64_t now_us=1000000;
inline int critical_depth=0, sem_failures=0, create_count=0, give_count=0, take_count=0, delete_count=0;
inline int socket_result=-1, fail_option=-1, close_count=0, shutdown_count=0, receive_count=0, send_count=0;
inline int recv_timeout_us=0, send_timeout_us=0;
inline bool create_fails=false;
inline TaskHandle_t current=nullptr;
inline std::vector<std::unique_ptr<HostTask>> tasks;
inline std::vector<std::unique_ptr<HostSemaphore>> semaphores;
inline std::function<void()> create_hook, take_hook, give_hook;
inline std::function<int(void*,size_t,sockaddr*,socklen_t*)> recv_hook;
inline std::function<void(const void*,size_t)> send_hook;
inline void check(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"ASSERT: %s\n",message);std::exit(1);} }
inline void run(size_t index) {
 check(index<tasks.size(),"scheduled task exists"); auto* task=tasks[index].get();
 check(!task->done,"task is not run twice"); const auto old=current; current=task;
 task->fn(task->arg); task->done=true; current=old;
}
}
inline void portENTER_CRITICAL(portMUX_TYPE* m){++m->depth;++host::critical_depth;}
inline void portEXIT_CRITICAL(portMUX_TYPE* m){--m->depth;--host::critical_depth;host::check(m->depth>=0,"balanced critical section");}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){
 if(host::sem_failures>0){--host::sem_failures;return nullptr;}
 host::semaphores.emplace_back(new HostSemaphore);return host::semaphores.back().get();
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t){
 ++host::take_count;host::check(s&&!s->deleted,"take uses a live semaphore");
 if(!s->available && host::take_hook) host::take_hook();
 if(!s->available)return pdFALSE;s->available=false;return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s){
 host::check(s&&!s->deleted,"worker must not signal a deleted semaphore");
 ++host::give_count;if(host::give_hook)host::give_hook(); s->available=true;return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t s){host::check(s&&!s->deleted,"delete live semaphore once");s->deleted=true;++host::delete_count;}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,const char*,uint32_t,void* arg,unsigned,TaskHandle_t* out,int){
 ++host::create_count;if(host::create_hook)host::create_hook();if(host::create_fails)return pdFALSE;
 host::tasks.emplace_back(new HostTask{fn,arg});if(out)*out=host::tasks.back().get();return pdPASS;
}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return host::current;}
inline void vTaskDelete(TaskHandle_t){}
inline void vTaskDelay(TickType_t){}
inline int64_t esp_timer_get_time(){return static_cast<int64_t>(host::now_us);}
constexpr int ESP_MAC_WIFI_STA=0;
inline int esp_read_mac(uint8_t* out,int){const uint8_t mac[]={2,0,0,0,0,2};std::memcpy(out,mac,6);return ESP_OK;}
inline const char* esp_err_to_name(esp_err_t){return "host-error";}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)

namespace lg {
enum class NodeRole:uint8_t { Auto=0, Master=1, Worker=2 };
enum class CueRunnerState:uint8_t { Stopped=0, Holding=1, Fading=2, Paused=3, Manual=4 };
enum class ShowTransportMode:uint8_t { Standalone=0, Scene=1, Timeline=2, Cue=3 };
struct CueTransportSnapshot {
 bool active=false;uint8_t list_index=0,state=0,current_cue=0,next_cue=0;
 uint16_t manual_fade_q15=0;uint64_t cue_anchor_ms=0;uint32_t paused_elapsed_ms=0,generation=0;
};
struct ShowTransportSnapshot {
 ShowTransportMode mode=ShowTransportMode::Standalone;uint8_t active_scene=0;uint16_t bpm=120;
 uint8_t cue_list=0,cue_index=0,cue_next=0;CueRunnerState cue_state=CueRunnerState::Stopped;
 uint32_t cue_generation=0,cue_elapsed_ms=0;uint16_t manual_fade_q15=0;
 uint32_t transport_generation=0;uint64_t timeline_anchor_ms=0;uint32_t timeline_active_checksum=0;
 uint32_t cue_list_checksum=0;uint16_t cue_transition_progress_q15=0;uint8_t cue_paused_from=0,cue_source_scene=0;
 CueTransportSnapshot cue{};
};
struct DeviceConfig {uint32_t sync_token=1234;bool sync_enabled=true;uint8_t sync_role=2;};
struct ConfigStore {
 DeviceConfig value{};int result=ESP_OK;int reads=0;
 int load_config(DeviceConfig& c){++reads;c=value;return result;}
};
struct LightingRuntime {
 int feed_count=0,activations=0;uint8_t scene=0;bool activate_ok=true;
 uint8_t active_scene_index(){return scene;}
 void feed_watchdog(){++feed_count;}
 bool activate_scene_checked(uint8_t index){++activations;scene=index;return activate_ok;}
};
}
inline int host_socket(int,int,int){return host::socket_result;}
inline int host_setsockopt(int,int,int opt,const void* val,socklen_t){
 if(opt==SO_RCVTIMEO)host::recv_timeout_us=static_cast<const timeval*>(val)->tv_usec;
 if(opt==SO_SNDTIMEO)host::send_timeout_us=static_cast<const timeval*>(val)->tv_usec;
 return opt==host::fail_option ? -1:0;
}
inline int host_bind(int,const sockaddr*,socklen_t){return 0;}
inline int host_close(int){++host::close_count;return 0;}
inline int host_shutdown(int,int){++host::shutdown_count;return 0;}
inline int host_recvfrom(int,void* data,size_t len,int,sockaddr* from,socklen_t* slen){
 ++host::receive_count;host::check(host::receive_count<50,"receive loop must terminate in host scenario");
 return host::recv_hook?host::recv_hook(data,len,from,slen):-1;
}
inline int host_sendto(int,const void* data,size_t len,int,const sockaddr*,socklen_t){++host::send_count;if(host::send_hook)host::send_hook(data,len);return static_cast<int>(len);}
