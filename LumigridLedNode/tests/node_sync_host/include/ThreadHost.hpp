#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_TIMEOUT=0x107;
using BaseType_t=int;using UBaseType_t=unsigned;using TickType_t=uint32_t;
constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1;constexpr TickType_t portMAX_DELAY=UINT32_MAX;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
struct portMUX_TYPE{std::recursive_mutex mutex;};
#define portMUX_INITIALIZER_UNLOCKED {}
struct HostSemaphore {std::mutex mutex;std::condition_variable cv;bool available=false;};
using SemaphoreHandle_t=HostSemaphore*;
using TaskFunction_t=void(*)(void*);
struct HostTask {std::thread thread;~HostTask(){if(thread.joinable())thread.join();}};
using TaskHandle_t=HostTask*;
namespace host {
inline thread_local int critical_depth=0;
inline thread_local TaskHandle_t current=nullptr;
inline std::atomic<int> create_count{0},socket_count{0},shutdown_count{0},delete_count{0},send_count{0};
// Set before start and keep immutable until the worker has joined.
inline std::function<int(void*,size_t,sockaddr*,socklen_t*)> recv_hook;
inline std::mutex tasks_mutex;
inline std::vector<std::unique_ptr<HostTask>> tasks;
inline void check(bool ok,const char* msg){if(!ok){std::fprintf(stderr,"ASSERT %s\n",msg);std::exit(1);}}
inline void join(){std::vector<std::unique_ptr<HostTask>> local;{std::lock_guard<std::mutex> lock(tasks_mutex);local.swap(tasks);}local.clear();}
}
inline void portENTER_CRITICAL(portMUX_TYPE* m){m->mutex.lock();++host::critical_depth;}
inline void portEXIT_CRITICAL(portMUX_TYPE* m){--host::critical_depth;m->mutex.unlock();}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){return new HostSemaphore;}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t ticks){
 std::unique_lock<std::mutex> lock(s->mutex);
 if(!s->cv.wait_for(lock,std::chrono::milliseconds(ticks),[&]{return s->available;}))return pdFALSE;
 s->available=false;return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s){std::lock_guard<std::mutex> lock(s->mutex);s->available=true;s->cv.notify_one();return pdTRUE;}
inline void vSemaphoreDelete(SemaphoreHandle_t s){delete s;++host::delete_count;}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,const char*,uint32_t,void* arg,unsigned,TaskHandle_t* out,int){
 auto task=std::make_unique<HostTask>();auto* ptr=task.get();if(out)*out=ptr;
 {std::lock_guard<std::mutex> lock(host::tasks_mutex);host::tasks.push_back(std::move(task));}
 ++host::create_count;ptr->thread=std::thread([=]{host::current=ptr;fn(arg);host::current=nullptr;});return pdPASS;
}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return host::current;}
inline void vTaskDelete(TaskHandle_t){}
inline void vTaskDelay(TickType_t t){std::this_thread::sleep_for(std::chrono::milliseconds(t));}
inline int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
constexpr int ESP_MAC_WIFI_STA=0;
inline int esp_read_mac(uint8_t* out,int){const uint8_t m[]={2,0,0,0,0,2};std::memcpy(out,m,6);return ESP_OK;}
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
 std::atomic<int> feed_count{0},activations{0};std::atomic<uint8_t> scene{0};bool activate_ok=true;
 uint8_t active_scene_index(){return scene;}
 void feed_watchdog(){++feed_count;}
 bool activate_scene_checked(uint8_t index){++activations;scene=index;return activate_ok;}
};
}

inline int host_socket(int,int,int){return ++host::socket_count+10;}
inline int host_setsockopt(int,int,int,const void*,socklen_t){return 0;}
inline int host_bind(int,const sockaddr*,socklen_t){return 0;}
inline int host_close(int){return 0;}
inline int host_shutdown(int,int){++host::shutdown_count;return 0;}
inline int host_recvfrom(int,void* data,size_t len,int,sockaddr* from,socklen_t* slen){
 if(host::recv_hook)return host::recv_hook(data,len,from,slen);
 std::this_thread::sleep_for(std::chrono::milliseconds(1));return -1;
}
inline int host_sendto(int,const void*,size_t len,int,const sockaddr*,socklen_t){++host::send_count;return static_cast<int>(len);}
