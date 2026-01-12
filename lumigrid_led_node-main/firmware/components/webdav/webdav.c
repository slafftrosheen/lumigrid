#include "webdav.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static const char* TAG="WEBDAV";

static esp_err_t send_status(httpd_req_t* r,int code,const char* msg){
  char b[64]; 
  snprintf(b,sizeof(b),"%d %s", code, msg?msg:"");
  httpd_resp_set_status(r,b);
  httpd_resp_set_hdr(r,"Content-Length","0");
  httpd_resp_send(r, NULL, 0);
  return ESP_OK;
}

static bool map_path(httpd_req_t* r, char* out, size_t n){
  const char* u=r->uri; // /dav/...
  if (strncmp(u,"/dav",4)!=0) return false;
  
  // Security: Check for directory traversal attempts
  if (strstr(u, "..") != NULL) {
    ESP_LOGW(TAG, "Path traversal attempt blocked: %s", u);
    return false;
  }
  
  int written = snprintf(out,n,"/storage%s", u+4);
  if (written < 0 || written >= (int)n) return false; // overflow check
  
  // Remove trailing slash if present
  size_t len = strlen(out);
  if (len > 0 && out[len-1]=='/') {
    out[len-1]='\0';
  }
  
  // Security: Verify path stays within /storage
  if (strncmp(out, "/storage", 8) != 0) {
    ESP_LOGW(TAG, "Path escape attempt blocked: %s", out);
    return false;
  }
  
  return true;
}

static esp_err_t dav_get(httpd_req_t* r){
  char p[256]; 
  if(!map_path(r,p,sizeof(p))) return send_status(r,400,"Bad Request");
  
  struct stat st; 
  if (stat(p,&st)<0) return send_status(r,404,"Not Found");
  
  if (S_ISDIR(st.st_mode)){
    // Simple HTML listing (also works with some DAV clients)
    httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(r,"<ul>");
    DIR* d=opendir(p); 
    if (d) {
      struct dirent* e; 
      while((e=readdir(d))){
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        httpd_resp_sendstr_chunk(r,"<li><a href=\""); 
        httpd_resp_sendstr_chunk(r,r->uri);
        if (r->uri[strlen(r->uri)-1]!='/') httpd_resp_sendstr_chunk(r,"/");
        httpd_resp_sendstr_chunk(r,e->d_name); 
        httpd_resp_sendstr_chunk(r,"\">");
        httpd_resp_sendstr_chunk(r,e->d_name); 
        httpd_resp_sendstr_chunk(r,"</a></li>");
      } 
      closedir(d);
    }
    httpd_resp_sendstr_chunk(r,"</ul>"); 
    httpd_resp_sendstr_chunk(r,NULL);
    return ESP_OK;
  } else {
    FILE* f=fopen(p,"rb"); 
    if(!f) return send_status(r,404,"Not Found");
    httpd_resp_set_type(r,"application/octet-stream");
    char buf[1024]; 
    int n; 
    while((n=fread(buf,1,sizeof(buf),f))>0){ 
      httpd_resp_send_chunk(r,buf,n); 
    }
    fclose(f); 
    httpd_resp_send_chunk(r,NULL,0); 
    return ESP_OK;
  }
}

static esp_err_t dav_put(httpd_req_t* r){
  char p[256]; 
  if(!map_path(r,p,sizeof(p))) return send_status(r,400,"Bad Request");
  
  // Security: Limit file upload size (10MB max)
  #define MAX_UPLOAD_SIZE (10 * 1024 * 1024)
  if (r->content_len > MAX_UPLOAD_SIZE) {
    ESP_LOGW(TAG, "File too large: %d bytes (max %d)", r->content_len, MAX_UPLOAD_SIZE);
    return send_status(r,413,"Payload Too Large");
  }
  
  // ensure parent dir
  for(char* c=p+1; *c; ++c){ 
    if(*c=='/'){ 
      *c=0; 
      mkdir(p,0777); 
      *c='/'; 
    } 
  }
  
  FILE* f=fopen(p,"wb"); 
  if(!f) return send_status(r,409,"Conflict");
  
  char buf[1024]; 
  int n;
  size_t total = 0;
  while((n=httpd_req_recv(r,buf,sizeof(buf)))>0){ 
    total += n;
    if (total > MAX_UPLOAD_SIZE) {
      fclose(f);
      unlink(p);
      return send_status(r,413,"Payload Too Large");
    }
    fwrite(buf,1,n,f); 
  }
  fclose(f); 
  return send_status(r,201,"Created");
}

static esp_err_t dav_delete(httpd_req_t* r){
  char p[256]; 
  if(!map_path(r,p,sizeof(p))) return send_status(r,400,"Bad Request");
  
  struct stat st; 
  if (stat(p,&st)<0) return send_status(r,404,"Not Found");
  
  int rc = S_ISDIR(st.st_mode) ? rmdir(p) : unlink(p);
  return send_status(r, rc==0?204:409, rc==0?"No Content":"Conflict");
}

// PROPFIND minimal Depth:1 for directory listing (enough for Finder/Win)
// We answer with super-minimal XML multistatus.
static esp_err_t dav_propfind(httpd_req_t* r){
  char p[256]; 
  if(!map_path(r,p,sizeof(p))) return send_status(r,400,"Bad Request");
  
  httpd_resp_set_type(r,"application/xml; charset=utf-8");
  httpd_resp_sendstr_chunk(r,"<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">");
  
  struct stat st; 
  if (stat(p,&st)<0) { 
    httpd_resp_sendstr_chunk(r,"</d:multistatus>"); 
    httpd_resp_sendstr_chunk(r,NULL); 
    return ESP_OK; 
  }
  
  // Helper to emit response
  char b[512];
  
  // self
  httpd_resp_sendstr_chunk(r,"<d:response><d:href>");
  httpd_resp_sendstr_chunk(r, r->uri);
  httpd_resp_sendstr_chunk(r,"</d:href><d:propstat><d:prop>");
  if (S_ISDIR(st.st_mode)) {
    httpd_resp_sendstr_chunk(r,"<d:resourcetype><d:collection/></d:resourcetype>");
  } else {
    httpd_resp_sendstr_chunk(r,"<d:resourcetype/>");
  }
  snprintf(b,sizeof(b),"<d:getcontentlength>%u</d:getcontentlength>",(unsigned)st.st_size); 
  httpd_resp_sendstr_chunk(r,b);
  httpd_resp_sendstr_chunk(r,"</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>");
  
  if (S_ISDIR(st.st_mode)){
    DIR* d=opendir(p); 
    if (d) {
      struct dirent* e; 
      while((e=readdir(d))){
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char href[256]; 
        snprintf(href,sizeof(href), "%s%s%s", r->uri, (r->uri[strlen(r->uri)-1]=='/')?"":"/", e->d_name);
        char sub[256]; 
        snprintf(sub,sizeof(sub), "%s/%s", p, e->d_name);
        struct stat st2; 
        if (stat(sub,&st2) == 0) {
          httpd_resp_sendstr_chunk(r,"<d:response><d:href>");
          httpd_resp_sendstr_chunk(r, href);
          httpd_resp_sendstr_chunk(r,"</d:href><d:propstat><d:prop>");
          if (S_ISDIR(st2.st_mode)) {
            httpd_resp_sendstr_chunk(r,"<d:resourcetype><d:collection/></d:resourcetype>");
          } else {
            httpd_resp_sendstr_chunk(r,"<d:resourcetype/>");
          }
          snprintf(b,sizeof(b),"<d:getcontentlength>%u</d:getcontentlength>",(unsigned)st2.st_size); 
          httpd_resp_sendstr_chunk(r,b);
          httpd_resp_sendstr_chunk(r,"</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>");
        }
      } 
      closedir(d);
    }
  }
  httpd_resp_sendstr_chunk(r,"</d:multistatus>"); 
  httpd_resp_sendstr_chunk(r,NULL);
  return ESP_OK;
}

// Handler for WebDAV OPTIONS - advertises capabilities
static esp_err_t dav_options(httpd_req_t* r){
  // Set WebDAV-specific headers
  httpd_resp_set_hdr(r, "DAV", "1");  // Basic WebDAV support
  httpd_resp_set_hdr(r, "Allow", "OPTIONS, GET, PUT, DELETE");
  httpd_resp_set_hdr(r, "Accept-Ranges", "bytes");
  httpd_resp_set_hdr(r, "Content-Length", "0");
  httpd_resp_send(r, NULL, 0);
  return ESP_OK;
}

void webdav_register(httpd_handle_t s){
  const httpd_uri_t u_opt ={.uri="/dav*", .method=HTTP_OPTIONS, .handler=dav_options};
  const httpd_uri_t u_get ={.uri="/dav*", .method=HTTP_GET,     .handler=dav_get};
  const httpd_uri_t u_put ={.uri="/dav*", .method=HTTP_PUT,     .handler=dav_put};
  const httpd_uri_t u_del ={.uri="/dav*", .method=HTTP_DELETE,  .handler=dav_delete};
  
  httpd_register_uri_handler(s,&u_opt);
  httpd_register_uri_handler(s,&u_get);
  httpd_register_uri_handler(s,&u_put);
  httpd_register_uri_handler(s,&u_del);
  
  ESP_LOGI(TAG,"WebDAV mounted at /dav -> /storage");
  ESP_LOGI(TAG,"Supported methods: OPTIONS, GET (listing), PUT (upload), DELETE");
  ESP_LOGW(TAG,"Note: PROPFIND/MKCOL/COPY/MOVE not supported due to ESP-IDF HTTP server limitations");
  ESP_LOGI(TAG,"For full WebDAV support, consider using a custom HTTP server or modifying ESP-IDF");
}
