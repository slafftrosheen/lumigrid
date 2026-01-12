#include "layout.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "storage_fs.h"

static const char* TAG="LAYOUT";
static layout_t s;
static lg_channel_t s_ch[8];
static int s_cnt=8;

// Default GPIO assignments for addressable LED channels
static const uint8_t DEFAULT_ALED_GPIO[8] = {16, 4, 17, 18, 23, 5, 19, 32};

static void defaults(void){
  memset(&s,0,sizeof(s));
  for (int i=0;i<4;i++){ 
    s.ch[i].type=LAYOUT_STRIP_WS2812B; 
    s.ch[i].order=LAYOUT_ORDER_GRB; 
    s.ch[i].length=60; 
    s.ch[i].pwr_pwm=-1; 
  }
  // Batch 24: default extended channel config
  for (int i=0;i<8;i++){
    s_ch[i]=(lg_channel_t){
      .type=LTYPE_WS2812B, .order=LORDER_GRB, .gpio=DEFAULT_ALED_GPIO[i],
      .pixels=60, .pwm_r=255, .pwm_g=255, .pwm_b=255, .pwm_w=255, .enabled = (i<4)
    };
  }
}

void layout_init(void){
  defaults();
  char path[128];
  if (storage_fs_path(path, sizeof(path), "layout.json")){
    (void)layout_load(path);
  }
}

static cJSON* dump(void){
  cJSON* root = cJSON_CreateObject();
  cJSON* arr = cJSON_AddArrayToObject(root,"channels");
  for (int i=0;i<4;i++){
    cJSON* ch = cJSON_CreateObject();
    cJSON_AddNumberToObject(ch,"type", s.ch[i].type);
    cJSON_AddNumberToObject(ch,"order",s.ch[i].order);
    cJSON_AddNumberToObject(ch,"length",s.ch[i].length);
    cJSON_AddNumberToObject(ch,"pwr_pwm",s.ch[i].pwr_pwm);
    cJSON* segs = cJSON_AddArrayToObject(ch,"segments");
    for (int k=0;k<s.ch[i].segments_count;k++){
      cJSON* sg = cJSON_CreateObject();
      cJSON_AddNumberToObject(sg,"start", s.ch[i].segments[k].start);
      cJSON_AddNumberToObject(sg,"len",   s.ch[i].segments[k].len);
      cJSON_AddBoolToObject  (sg,"reverse", s.ch[i].segments[k].reverse);
      cJSON_AddNumberToObject(sg,"mirror", s.ch[i].segments[k].mirror);
      cJSON_AddItemToArray(segs, sg);
    }
    cJSON_AddItemToArray(arr,ch);
  }
  return root;
}

const layout_t* layout_get(void){ return &s; }

int layout_save(const char* path){
  cJSON* r = dump();
  char* txt = cJSON_PrintUnformatted(r);
  FILE* f = fopen(path,"w"); 
  if(!f){ cJSON_free(txt); cJSON_Delete(r); return -1; }
  fwrite(txt,1,strlen(txt),f); 
  fclose(f);
  cJSON_free(txt); 
  cJSON_Delete(r);
  return 0;
}

int layout_load(const char* path){
  FILE* f = fopen(path,"r"); 
  if(!f){ return -1; }
  fseek(f,0,SEEK_END); 
  long sz=ftell(f); 
  fseek(f,0,SEEK_SET);
  char* buf = malloc(sz+1); 
  if(!buf){ fclose(f); return -1; }
  fread(buf,1,sz,f); 
  buf[sz]=0; 
  fclose(f);
  
  cJSON* r = cJSON_Parse(buf); 
  free(buf);
  if(!r){ return -1; }
  
  cJSON* chans = cJSON_GetObjectItem(r,"channels");
  if (!cJSON_IsArray(chans)){ cJSON_Delete(r); return -1; }
  
  defaults();
  int i=0; 
  cJSON* it=NULL; 
  cJSON_ArrayForEach(it,chans){
    if(i>=4) break;
    cJSON* v;
    if((v=cJSON_GetObjectItem(it,"type")))  s.ch[i].type  = v->valueint;
    if((v=cJSON_GetObjectItem(it,"order"))) s.ch[i].order = v->valueint;
    if((v=cJSON_GetObjectItem(it,"length")))s.ch[i].length= v->valueint;
    if((v=cJSON_GetObjectItem(it,"pwr_pwm")))s.ch[i].pwr_pwm= v->valueint;
    
    cJSON* segs=cJSON_GetObjectItem(it,"segments");
    s.ch[i].segments_count=0;
    if (cJSON_IsArray(segs)){
      cJSON* sg=NULL; 
      cJSON_ArrayForEach(sg,segs){
        if (s.ch[i].segments_count>=12) break;
        segment_t* S = &s.ch[i].segments[s.ch[i].segments_count++];
        S->start = cJSON_GetObjectItem(sg,"start")->valueint;
        S->len   = cJSON_GetObjectItem(sg,"len")->valueint;
        cJSON* rv= cJSON_GetObjectItem(sg,"reverse");
        S->reverse = cJSON_IsTrue(rv);
        cJSON* mi= cJSON_GetObjectItem(sg,"mirror");
        S->mirror = mi ? mi->valueint : 0;
      }
    }
    i++;
  }
  cJSON_Delete(r);
  return 0;
}

int layout_virtual_len(int ch){
  if (ch<0||ch>3) return 0;
  const channel_layout_t* C = &s.ch[ch];
  if (C->segments_count==0) return C->length;
  int sum=0;
  for (int i=0;i<C->segments_count;i++){
    int mul = C->segments[i].mirror ? 2 : 1; // A + A' mirror
    sum += C->segments[i].len * mul;
  }
  return sum;
}

// builds mapping of virtual index -> physical index (0..length-1)
int layout_build_map(int ch, uint16_t* out_map, int cap){
  if (ch<0||ch>3 || !out_map) return 0;
  const channel_layout_t* C = &s.ch[ch];
  if (C->segments_count==0){
    int N = C->length < cap ? C->length : cap;
    for (int i=0;i<N;i++) out_map[i]=i;
    return N;
  }
  int w=0;
  for (int sidx=0;sidx<C->segments_count;sidx++){
    const segment_t* S = &C->segments[sidx];
    for (int i=0;i<S->len && w<cap;i++){
      out_map[w++] = S->reverse ? (S->start + (S->len-1 - i)) : (S->start + i);
    }
    if (S->mirror){
      for (int i=S->len-1;i>=0 && w<cap;i--){
        out_map[w++] = S->reverse ? (S->start + (S->len-1 - i)) : (S->start + i);
      }
    }
  }
  return w;
}

// ===== Batch 24: Extended layout API implementation =====

static lg_order_t parse_order(const char* s){
  if (!s) return LORDER_GRB;
  if (!strcmp(s,"RGB"))  return LORDER_RGB;
  if (!strcmp(s,"GRB"))  return LORDER_GRB;
  if (!strcmp(s,"BGR"))  return LORDER_BGR;
  if (!strcmp(s,"RGBW")) return LORDER_RGBW;
  if (!strcmp(s,"GRBW")) return LORDER_GRBW;
  if (!strcmp(s,"WRGB")) return LORDER_WRGB;
  return LORDER_GRB;
}

static lg_type_t parse_type(const char* s){
  if (!s) return LTYPE_WS2812B;
  if (!strcmp(s,"WS2812B"))      return LTYPE_WS2812B;
  if (!strcmp(s,"SK6812_RGBW"))  return LTYPE_SK6812_RGBW;
  if (!strcmp(s,"PWM"))          return LTYPE_PWM;
  return LTYPE_WS2812B;
}

static void load_extended_layout(void){
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "layout.json")){
    ESP_LOGW(TAG,"/storage/layout.json path not available, using defaults");
    return;
  }
  
  FILE* f=fopen(path,"r");
  if (!f){ 
    ESP_LOGW(TAG,"/storage/layout.json not found, using defaults"); 
    return; 
  }
  
  char buf[4096]; 
  int n=fread(buf,1,sizeof(buf)-1,f); 
  buf[n]=0; 
  fclose(f);
  
  cJSON* root=cJSON_Parse(buf);
  if(!root){ 
    ESP_LOGW(TAG,"JSON parse error, keep defaults"); 
    return; 
  }
  
  cJSON* arr=cJSON_GetObjectItem(root,"channels");
  if(!cJSON_IsArray(arr)){ 
    cJSON_Delete(root); 
    return; 
  }
  
  int m=cJSON_GetArraySize(arr); 
  if(m>8) m=8;
  
  for (int i=0;i<m;i++){
    cJSON* c=cJSON_GetArrayItem(arr,i);
    if(!cJSON_IsObject(c)) continue;
    
    cJSON* type_obj = cJSON_GetObjectItem(c,"type");
    cJSON* order_obj = cJSON_GetObjectItem(c,"order");
    const char* t = type_obj && cJSON_IsString(type_obj) ? type_obj->valuestring : NULL;
    const char* o = order_obj && cJSON_IsString(order_obj) ? order_obj->valuestring : "GRB";
    
    s_ch[i].type   = parse_type(t);
    s_ch[i].order  = parse_order(o);
    
    cJSON* gpio_obj = cJSON_GetObjectItem(c,"gpio");
    if(gpio_obj && cJSON_IsNumber(gpio_obj)) s_ch[i].gpio = (uint8_t)gpio_obj->valueint;
    
    cJSON* pixels_obj = cJSON_GetObjectItem(c,"pixels");
    if(pixels_obj && cJSON_IsNumber(pixels_obj)) s_ch[i].pixels = (uint16_t)pixels_obj->valueint;
    
    cJSON* pwm_r_obj = cJSON_GetObjectItem(c,"pwm_r");
    if(pwm_r_obj && cJSON_IsNumber(pwm_r_obj)) s_ch[i].pwm_r = (uint8_t)pwm_r_obj->valueint;
    
    cJSON* pwm_g_obj = cJSON_GetObjectItem(c,"pwm_g");
    if(pwm_g_obj && cJSON_IsNumber(pwm_g_obj)) s_ch[i].pwm_g = (uint8_t)pwm_g_obj->valueint;
    
    cJSON* pwm_b_obj = cJSON_GetObjectItem(c,"pwm_b");
    if(pwm_b_obj && cJSON_IsNumber(pwm_b_obj)) s_ch[i].pwm_b = (uint8_t)pwm_b_obj->valueint;
    
    cJSON* pwm_w_obj = cJSON_GetObjectItem(c,"pwm_w");
    if(pwm_w_obj && cJSON_IsNumber(pwm_w_obj)) s_ch[i].pwm_w = (uint8_t)pwm_w_obj->valueint;
    
    cJSON* enabled_obj = cJSON_GetObjectItem(c,"enabled");
    if(enabled_obj) s_ch[i].enabled = cJSON_IsTrue(enabled_obj);
  }
  
  cJSON_Delete(root);
  ESP_LOGI(TAG,"Extended layout loaded");
}

const lg_channel_t* layout_get_channel(int ch){ 
  // Try to load extended layout on first access if not already loaded
  static bool loaded = false;
  if (!loaded) {
    load_extended_layout();
    loaded = true;
  }
  return (ch>=0 && ch<8) ? &s_ch[ch] : NULL; 
}

int layout_channel_count(void){ 
  return s_cnt; 
}

int layout_addr_channels(void){
  int k=0; 
  for(int i=0;i<s_cnt;i++){ 
    if(s_ch[i].enabled && (s_ch[i].type==LTYPE_WS2812B || s_ch[i].type==LTYPE_SK6812_RGBW)) k++; 
  }
  return k;
}

bool layout_is_rgbw(int ch){
  const lg_channel_t* C=layout_get_channel(ch);
  if(!C) return false;
  if(C->type==LTYPE_SK6812_RGBW) return true;
  if(C->type==LTYPE_PWM && C->pwm_w!=255) return true;
  return false;
}
