#include "capport.h"
#include "rest_api.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG="CAPPORT";
static int s_dns=-1; 
static uint32_t s_ap_ip;

// Embedded setup wizard HTML
extern const uint8_t setup_wizard_html_start[] asm("_binary_setup_wizard_html_start");
extern const uint8_t setup_wizard_html_end[] asm("_binary_setup_wizard_html_end");

typedef struct __attribute__((packed)){
  uint16_t id, flags, qdcount, ancount, nscount, arcount;
} dns_hdr_t;

static void put_u16(uint8_t* p, uint16_t v){ 
  p[0]=v>>8; 
  p[1]=v&0xff; 
}

static void* answer_any_a(const char* name, uint32_t ip, int* outlen){
  // minimal DNS response with single A record, no compression (safe & tiny)
  static uint8_t buf[512];
  memset(buf,0,sizeof(buf));
  dns_hdr_t* h=(dns_hdr_t*)buf;
  h->id=htons(0x1234); 
  h->flags=htons(0x8180); 
  h->qdcount=htons(1); 
  h->ancount=htons(1);
  
  int o=sizeof(dns_hdr_t);
  // encode QNAME as labels
  const char* s=name; 
  while(*s){
    const char* dot=strchr(s,'.'); 
    int len=dot ? (dot-s) : strlen(s);
    buf[o++]=len; 
    memcpy(&buf[o],s,len); 
    o+=len; 
    if(!dot) break; 
    s=dot+1;
  }
  buf[o++]=0;
  put_u16(&buf[o],1); o+=2; // QTYPE A
  put_u16(&buf[o],1); o+=2; // QCLASS IN
  
  // ANS: reuse the same name again
  s=name; 
  while(*s){
    const char* dot=strchr(s,'.'); 
    int len=dot ? (dot-s) : strlen(s);
    buf[o++]=len; 
    memcpy(&buf[o],s,len); 
    o+=len; 
    if(!dot) break; 
    s=dot+1;
  }
  buf[o++]=0;
  put_u16(&buf[o],1); o+=2;       // TYPE A
  put_u16(&buf[o],1); o+=2;       // CLASS IN
  buf[o++]=0; buf[o++]=0; buf[o++]=0; buf[o++]=30; // TTL 30s
  put_u16(&buf[o],4); o+=2;       // RDLENGTH
  memcpy(&buf[o], &ip, 4); o+=4;  // RDATA
  *outlen=o; 
  return buf;
}

static void dns_task(void* arg){
  (void)arg;
  s_dns = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_dns < 0) {
    ESP_LOGE(TAG, "Failed to create DNS socket");
    vTaskDelete(NULL);
    return;
  }
  
  struct sockaddr_in a={0}; 
  a.sin_family=AF_INET; 
  a.sin_port=htons(53); 
  a.sin_addr.s_addr=INADDR_ANY;
  
  if (bind(s_dns,(struct sockaddr*)&a,sizeof(a)) < 0) {
    ESP_LOGE(TAG, "DNS bind failed");
    close(s_dns);
    s_dns = -1;
    vTaskDelete(NULL);
    return;
  }
  
  ESP_LOGI(TAG,"DNS up on :53 answering with %s", inet_ntoa(*(struct in_addr*)&s_ap_ip));

  while(1){
    uint8_t rx[512]; 
    struct sockaddr_in from; 
    socklen_t fl=sizeof(from);
    int n=recvfrom(s_dns,rx,sizeof(rx),0,(struct sockaddr*)&from,&fl);
    if(n<12) continue;
    
    // parse QNAME into dot string (best-effort with validation)
    int o=12; 
    char qn[256]; 
    int ql=0; 
    while(o<n && rx[o]!=0 && ql<250){ 
      int L=rx[o++];
      // Security: Validate label length (DNS labels max 63, check for overflow)
      if (L > 63 || L > n - o || L > (int)(250 - ql)) {
        ESP_LOGW(TAG, "Malformed DNS packet, invalid label length: %d", L);
        break;
      }
      while(L-- && o<n){ 
        qn[ql++]=rx[o++]; 
      } 
      if(rx[o]!=0 && ql<250) qn[ql++]='.'; 
    }
    if(o>=n || ql==0) continue; 
    qn[ql]=0;
    
    int out; 
    void* ans=answer_any_a(qn,s_ap_ip,&out);
    // copy tx but use rx ID
    ((dns_hdr_t*)ans)->id = *(uint16_t*)rx;
    sendto(s_dns, ans, out, 0, (struct sockaddr*)&from, fl);
  }
}

static esp_err_t http_captive(httpd_req_t* r){ 
  httpd_resp_set_status(r,"302 Found"); 
  httpd_resp_set_hdr(r,"Location","/setup"); 
  return httpd_resp_sendstr(r,""); 
}

static esp_err_t http_setup_wizard(httpd_req_t* r){
  httpd_resp_set_type(r, "text/html");
  httpd_resp_send(r, (const char*)setup_wizard_html_start, 
                  setup_wizard_html_end - setup_wizard_html_start);
  return ESP_OK;
}

void capport_register_endpoints(httpd_handle_t srv) {
  if (!srv) {
    ESP_LOGE(TAG, "Cannot register captive portal endpoints: server is NULL");
    return;
  }
  
  const httpd_uri_t u_setup={.uri="/setup",.method=HTTP_GET,.handler=http_setup_wizard};
  const httpd_uri_t u_root={.uri="/",.method=HTTP_GET,.handler=http_setup_wizard};
  const httpd_uri_t u1={.uri="/generate_204",.method=HTTP_GET,.handler=http_captive};
  const httpd_uri_t u2={.uri="/hotspot-detect.html",.method=HTTP_GET,.handler=http_captive};
  const httpd_uri_t u3={.uri="/ncsi.txt",.method=HTTP_GET,.handler=http_captive};
  httpd_register_uri_handler(srv,&u_setup);
  httpd_register_uri_handler(srv,&u_root); 
  httpd_register_uri_handler(srv,&u1); 
  httpd_register_uri_handler(srv,&u2); 
  httpd_register_uri_handler(srv,&u3);
  ESP_LOGI(TAG, "Captive portal and setup wizard endpoints registered");
}

void capport_start(uint32_t ap_ip){
  s_ap_ip=ap_ip;
  BaseType_t ret = xTaskCreate(dns_task,"dns_captive",4096,NULL,4,NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create DNS task");
    return;
  }
  ESP_LOGI(TAG, "Captive portal DNS started");
}

void capport_stop(void){
  if(s_dns>=0){ 
    close(s_dns); 
    s_dns=-1; 
  }
}
