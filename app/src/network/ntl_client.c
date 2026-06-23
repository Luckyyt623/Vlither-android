#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_path.h"
#include <android/log.h>
#define ALOG(...) __android_log_print(ANDROID_LOG_DEBUG,"VlitherNTL",__VA_ARGS__)
#else
#define ALOG(...) printf("[NTL] " __VA_ARGS__)
#endif

#include "ntl_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../user.h"

/* ── log file ─────────────────────────────────────────────────────────── */

static FILE* s_log = NULL;

/* ── in-memory circular log ───────────────────────────────────────────── */

static char  s_log_buf[NTL_LOG_LINES][NTL_LOG_LINE_LEN];
static const char* s_log_ptrs[NTL_LOG_LINES];
static int   s_log_head  = 0;
static int   s_log_count = 0;

static void ntl_log(const char* fmt, ...) {
  char buf[NTL_LOG_LINE_LEN];
  va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);

  /* store in circular buffer */
  strncpy(s_log_buf[s_log_head], buf, NTL_LOG_LINE_LEN-1);
  s_log_buf[s_log_head][NTL_LOG_LINE_LEN-1] = '\0';
  s_log_ptrs[s_log_head] = s_log_buf[s_log_head];
  s_log_head = (s_log_head + 1) % NTL_LOG_LINES;
  if (s_log_count < NTL_LOG_LINES) s_log_count++;

  ALOG("%s", buf);
  if (s_log) { fprintf(s_log, "%s\n", buf); fflush(s_log); }
}

const char** ntl_get_log_lines(int* count) {
  /* Return lines in chronological order using a temp array */
  static const char* ordered[NTL_LOG_LINES];
  int start = (s_log_count < NTL_LOG_LINES)
              ? 0
              : s_log_head; /* oldest entry */
  for (int i = 0; i < s_log_count; i++)
    ordered[i] = s_log_buf[(start + i) % NTL_LOG_LINES];
  *count = s_log_count;
  return ordered;
}

static void log_open(void) {
  /* best-effort file log — UI log always works even if file fails */
#ifdef ANDROID
  s_log = fopen("/sdcard/Android/data/com.vlither/files/ntl_debug.log","w");
  if (!s_log) {
    char path[512];
    android_build_path(path, sizeof(path), "ntl_debug.log");
    s_log = fopen(path, "w");
  }
#else
  s_log = fopen("ntl_debug.log","w");
#endif
}

static void log_close(void) {
  if (s_log) { fclose(s_log); s_log=NULL; }
}

/* ── constants ────────────────────────────────────────────────────────── */

#define NTL_HOST              "ntl-slither.com"
#define NTL_POLL_INTERVAL     3.0
#define NTL_RESPONSE_BUF_SIZE 32768

/* ── per-request context — defined later, before event handler ─────────── */

#define MAX_PENDING_CHAT   16
#define MAX_OUTGOING_CHAT  16

typedef struct { char sender[32]; char text[128]; } pending_chat;
typedef struct { char user_id[9]; char last_msg[128]; } ntl_msg_cache;

static struct mg_mgr ntl_mgr;
static bool   ntl_running       = false;
static bool   request_in_flight = false;
static double next_poll_time    = 0;

static ntl_player  ntl_players_list[MAX_NTL_PLAYERS];
static int         ntl_player_count = 0;

static pending_chat  pending_chat_queue[MAX_PENDING_CHAT];
static int           pending_chat_count = 0;

static char outgoing_chat_queue[MAX_OUTGOING_CHAT][128];
static int  outgoing_chat_count = 0;

static ntl_msg_cache msg_caches[MAX_NTL_PLAYERS];
static int           msg_cache_count = 0;

static char ntl_response_buf[NTL_RESPONSE_BUF_SIZE];

/* ── string helpers ───────────────────────────────────────────────────── */

static const char* clean_nick(const char* n) {
  if (!n) return "";
  if (strlen(n)>=8) {
    bool hex=true;
    for(int i=0;i<8;i++){char c=n[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))){hex=false;break;}}
    if(hex) return n+8;
  }
  return n;
}

static void url_decode(const char* s, char* d, int m){
  int i=0;
  for(int j=0;s[j]&&i<m-1;){
    if(s[j]=='%'&&s[j+1]&&s[j+2]){char h[3]={s[j+1],s[j+2],0};d[i++]=(char)strtol(h,0,16);j+=3;}
    else if(s[j]=='+'){d[i++]=' ';j++;}
    else d[i++]=s[j++];
  }
  d[i]=0;
}

static void decode_html(const char* s, char* d, int m){
  int i=0;
  for(int j=0;s[j]&&i<m-1;){
    if(s[j]=='&'){
      if(!strncmp(s+j,"&nbsp;",6)){d[i++]=' ';j+=6;}
      else if(!strncmp(s+j,"&amp;",5)){d[i++]='&';j+=5;}
      else if(!strncmp(s+j,"&lt;",4)){d[i++]='<';j+=4;}
      else if(!strncmp(s+j,"&gt;",4)){d[i++]='>';j+=4;}
      else if(!strncmp(s+j,"&quot;",6)){d[i++]='"';j+=6;}
      else if(!strncmp(s+j,"&#39;",5)){d[i++]='\'';j+=5;}
      else d[i++]=s[j++];
    } else d[i++]=s[j++];
  }
  d[i]=0;
}

static void url_encode(const char* s, char* d, int m){
  int i=0;
  for(int j=0;s[j]&&i<m-3;j++){
    char c=s[j];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')
      d[i++]=c;
    else{sprintf(d+i,"%%%02X",(unsigned char)c);i+=3;}
  }
  d[i]=0;
}

static void extract_json_value(const char* obj, const char* key, char* dest, int max){
  dest[0]=0;
  char sk[64]; snprintf(sk,sizeof(sk),"\"%s\":",key);
  const char* p=strstr(obj,sk); if(!p) return;
  p+=strlen(sk);
  while(*p==' '||*p=='\t') p++;
  if(*p=='"'){p++;int i=0;while(*p&&*p!='"'&&i<max)dest[i++]=*p++;dest[i]=0;}
  else{int i=0;while(*p&&*p!=','&&*p!='}'&&*p!=' '&&*p!='\t'&&i<max)dest[i++]=*p++;dest[i]=0;}
}

/* ── public API ───────────────────────────────────────────────────────── */

void ntl_client_send_msg(const char* text) {
  if (outgoing_chat_count<MAX_OUTGOING_CHAT) {
    strncpy(outgoing_chat_queue[outgoing_chat_count],text,127);
    outgoing_chat_queue[outgoing_chat_count][127]=0;
    outgoing_chat_count++;
    ntl_log("queued outgoing msg: %s", text);
  }
}

bool ntl_client_poll_chat(char* sender, char* text) {
  if(pending_chat_count<=0) return false;
  strcpy(sender,pending_chat_queue[0].sender);
  strcpy(text,  pending_chat_queue[0].text);
  for(int i=1;i<pending_chat_count;i++) pending_chat_queue[i-1]=pending_chat_queue[i];
  pending_chat_count--;
  return true;
}

/* ── response parser ──────────────────────────────────────────────────── */

static void ntl_parse_response(const char* body, const char* my_nick) {
  ntl_log("parse_response: body_len=%d my_nick=%s", (int)strlen(body), my_nick);

  ntl_player temp[MAX_NTL_PLAYERS];
  int count=0;
  const char* p=body;

  while((p=strstr(p,"{"))&&count<MAX_NTL_PLAYERS){
    const char* end=strstr(p,"}"); if(!end) break;
    int len=(int)(end-p)+1;
    char* obj=malloc(len+1); if(!obj) break;
    memcpy(obj,p,len); obj[len]=0;

    ntl_player* pl=&temp[count];
    extract_json_value(obj,"nick", pl->nickname,31);
    extract_json_value(obj,"score",pl->score,   15);
    extract_json_value(obj,"srv",  pl->server,  31);
    extract_json_value(obj,"valx", pl->valx,    15);
    extract_json_value(obj,"valy", pl->valy,    15);
    char bv[16]={0}; extract_json_value(obj,"bot",bv,15); pl->is_bot=!strcmp(bv,"true");
    char sv[16]={0}; extract_json_value(obj,"sos",sv,15); pl->is_sos=!strcmp(sv,"true");
    extract_json_value(obj,"ver",  pl->ver,     15);
    extract_json_value(obj,"owner",pl->tlm,     31);
    extract_json_value(obj,"dt",   pl->dt,     127);
    extract_json_value(obj,"sid",  pl->sid,     15);

    char pl_msg[128]={0}; extract_json_value(obj,"msg",pl_msg,127);
    char decoded[128]={0};
    if(pl_msg[0]){char tmp[128]={0};url_decode(pl_msg,tmp,sizeof(tmp));decode_html(tmp,decoded,sizeof(decoded));}

    ntl_log("  player[%d]: nick=%s srv=%s sid=%s msg=%s",count,pl->nickname,pl->server,pl->sid,decoded);

    if(strcmp(pl->nickname,my_nick)!=0){
      char pl_id[9]={0};
      if(strlen(pl->nickname)>=8){strncpy(pl_id,pl->nickname,8);pl_id[8]=0;}
      if(pl_id[0]&&decoded[0]){
        int cidx=-1;
        for(int c=0;c<msg_cache_count;c++) if(!strcmp(msg_caches[c].user_id,pl_id)){cidx=c;break;}
        if(cidx>=0){
          if(strcmp(msg_caches[cidx].last_msg,decoded)!=0){
            strcpy(msg_caches[cidx].last_msg,decoded);
            if(pending_chat_count<MAX_PENDING_CHAT){
              strcpy(pending_chat_queue[pending_chat_count].sender,clean_nick(pl->nickname));
              strcpy(pending_chat_queue[pending_chat_count].text,decoded);
              pending_chat_count++;
            }
          }
        } else if(msg_cache_count<MAX_NTL_PLAYERS){
          int ni=msg_cache_count++;
          strcpy(msg_caches[ni].user_id,pl_id);
          strcpy(msg_caches[ni].last_msg,decoded);
          if(pending_chat_count<MAX_PENDING_CHAT){
            strcpy(pending_chat_queue[pending_chat_count].sender,clean_nick(pl->nickname));
            strcpy(pending_chat_queue[pending_chat_count].text,decoded);
            pending_chat_count++;
          }
        }
      } else if(pl_id[0]){
        for(int c=0;c<msg_cache_count;c++)
          if(!strcmp(msg_caches[c].user_id,pl_id)){msg_caches[c].last_msg[0]=0;break;}
      }
      count++;
    }
    free(obj); p=end+1;
  }
  ntl_player_count=count;
  memcpy(ntl_players_list,temp,sizeof(ntl_player)*count);
  ntl_log("parse done: %d players",count);
}

/* ── mongoose handler ─────────────────────────────────────────────────── */

/* response accumulation buffer per connection */
#define RESP_BUF_SIZE 65536
typedef struct ntl_req_ctx {
  char nick_with_id[128];
  char http_request[4200];
  char resp_buf[RESP_BUF_SIZE];
  int  resp_len;
} ntl_req_ctx;

static void ntl_event_handler(struct mg_connection* c, int ev, void* ev_data) {
  ntl_req_ctx* ctx=(ntl_req_ctx*)c->fn_data;
  if(ev==MG_EV_CONNECT){
    ntl_log("MG_EV_CONNECT — init TLS");
    struct mg_tls_opts opts={.skip_verification=1,.name=mg_str(NTL_HOST)};
    mg_tls_init(c,&opts);
  } else if(ev==MG_EV_TLS_HS){
    ntl_log("MG_EV_TLS_HS — TLS complete, sending HTTP request");
    if(ctx){
      ntl_log("request size: %d bytes",(int)strlen(ctx->http_request));
      mg_printf(c,"%s",ctx->http_request);
    }
  } else if(ev==MG_EV_READ){
    /* accumulate raw bytes — don't rely on MG_EV_HTTP_MSG parsing */
    if(ctx){
      struct mg_iobuf* r=(struct mg_iobuf*)ev_data;
      int space=RESP_BUF_SIZE-1-ctx->resp_len;
      int copy=(int)r->len < space ? (int)r->len : space;
      if(copy>0){
        memcpy(ctx->resp_buf+ctx->resp_len, r->buf, copy);
        ctx->resp_len+=copy;
        ctx->resp_buf[ctx->resp_len]='\0';
      }
      /* consume bytes so mongoose doesn't hold them */
      mg_iobuf_del(r,0,r->len);
    }
  } else if(ev==MG_EV_ERROR){
    ntl_log("MG_EV_ERROR: %s",(char*)ev_data);
    if(ctx){free(ctx);c->fn_data=NULL;}
    request_in_flight=false;
  } else if(ev==MG_EV_CLOSE){
    ntl_log("MG_EV_CLOSE — resp_len=%d", ctx ? ctx->resp_len : -1);
    if(ctx){
      if(ctx->resp_len>0){
        /* log first 300 chars of raw response */
        int prev=ctx->resp_len<300?ctx->resp_len:300;
        ntl_log("raw response: %.*s", prev, ctx->resp_buf);
        /* skip HTTP headers — find blank line \r\n\r\n or \n\n */
        char* body=strstr(ctx->resp_buf,"\r\n\r\n");
        if(body) body+=4;
        else { body=strstr(ctx->resp_buf,"\n\n"); if(body) body+=2; }
        if(body && *body)
          ntl_parse_response(body, ctx->nick_with_id);
        else
          ntl_log("no body found in response");
      } else {
        ntl_log("empty response — nothing to parse");
      }
      free(ctx); c->fn_data=NULL;
    }
    request_in_flight=false;
  }
}

/* ── issue one request ────────────────────────────────────────────────── */

static void ntl_issue_request(tenv* env) {
  if(request_in_flight){ntl_log("skipping — request already in flight");return;}

  tuser_data* usr=env->usr;
  user_settings* usrs=&usr->usrs;
  game_data* gdata=&usr->gdata;

  char active_tid[33]={0},active_auth[65]={0};
  if(usrs->ntl_active_team_idx>=0&&usrs->ntl_active_team_idx<usrs->ntl_team_count){
    strcpy(active_tid, usrs->ntl_teams[usrs->ntl_active_team_idx].team_id);
    strcpy(active_auth,usrs->ntl_teams[usrs->ntl_active_team_idx].auth_key);
  }
  ntl_log("ntl_issue_request: team_idx=%d tid=%s auth=%s",
          usrs->ntl_active_team_idx, active_tid, active_auth);

  if(!active_auth[0]||!active_tid[0]){ntl_log("no team configured — skipping");return;}

  if(!usrs->ntl_user_id[0]){
    for(int i=0;i<8;i++) sprintf(usrs->ntl_user_id+i,"%x",rand()%16);
    usrs->ntl_user_id[8]=0;
    ntl_log("generated user_id: %s",usrs->ntl_user_id);
    save_user_settings(usrs);
  }

  ntl_req_ctx* ctx=calloc(1,sizeof(ntl_req_ctx));
  if(!ctx){ntl_log("calloc failed");return;}

  snprintf(ctx->nick_with_id,sizeof(ctx->nick_with_id),"%s%s",usrs->ntl_user_id,usrs->nickname);

  char enc_nick[256]={0},enc_srv[128]={0},enc_dt[512]={0},enc_msg[1024]={0};
  url_encode(ctx->nick_with_id,enc_nick,sizeof(enc_nick));

  if(gdata->conn==CONNECTED&&gdata->curr_screen==PLAYING)
    url_encode(usrs->ipv4,enc_srv,sizeof(enc_srv));
  else strcpy(enc_srv,"_GAME_MENU_");

  char raw_dt[256];
  snprintf(raw_dt,sizeof(raw_dt),"FPS:%d@%dms@%dK/m",
           gdata->data.fps,gdata->data.ping,gdata->data.kills);
  url_encode(raw_dt,enc_dt,sizeof(enc_dt));

  float valx=gdata->data.view_xx,valy=gdata->data.view_yy;
  int slen=tdarray_length(gdata->data.snakes);
  if(slen>0){snake* me=gdata->data.snakes+(slen-1);valx=me->xx+me->fx;valy=me->yy+me->fy;}

  char raw_msg[512]={0};
  if(outgoing_chat_count>0){
    int ml=0;
    for(int i=0;i<outgoing_chat_count;i++){
      if(i>0&&ml+3<(int)sizeof(raw_msg)){strcat(raw_msg," | ");ml+=3;}
      int sl=(int)strlen(outgoing_chat_queue[i]);
      if(ml+sl<(int)sizeof(raw_msg)){strcat(raw_msg,outgoing_chat_queue[i]);ml+=sl;}
    }
    outgoing_chat_count=0;
  }
  if(raw_msg[0]) url_encode(raw_msg,enc_msg,sizeof(enc_msg));

  int my_sid=(gdata->conn==CONNECTED&&gdata->curr_screen==PLAYING)?gdata->data.snake_id:-1;

  char path[4096];
  snprintf(path,sizeof(path),
           "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s"
           "&score=%d&valx=%d&valy=%d&bot=%s&sos=false&food=false"
           "&srv=%s&msg=%s&dt=%s&i=%d&ver=9.19&di=1000",
           active_auth,active_tid,enc_nick,
           gdata->data.score,(int)valx,(int)valy,
           usrs->hotkeys[HOTKEY_BOT].active?"true":"false",
           enc_srv,enc_msg,enc_dt,my_sid);

  snprintf(ctx->http_request,sizeof(ctx->http_request),
           "GET %s HTTP/1.0\r\nHost: "NTL_HOST"\r\n"
           "User-Agent: Vlither/3.2\r\nConnection: close\r\n\r\n",path);

  /* Use plain mg_connect (not mg_http_connect) so we get raw MG_EV_READ
     events instead of relying on mongoose's HTTP client parser which
     silently drops MG_EV_HTTP_MSG on some server responses. */
  char host_port[64];
  snprintf(host_port,sizeof(host_port),"https://"NTL_HOST":443");
  ntl_log("connecting to: %s",host_port);

  if(!mg_connect(&ntl_mgr,host_port,ntl_event_handler,ctx)){
    ntl_log("mg_connect returned NULL — failed");
    free(ctx);
  } else {
    request_in_flight=true;
    ntl_log("connection opened ok");
  }
}

/* ── lifecycle ────────────────────────────────────────────────────────── */

void ntl_client_start(tenv* env) {
  if(ntl_running) return;
  log_open();
  ntl_log("=== ntl_client_start ===");
  mg_mgr_init(&ntl_mgr);
  ntl_running=true;
  request_in_flight=false;
  next_poll_time=0;
}

void ntl_client_stop(void) {
  if(!ntl_running) return;
  ntl_log("=== ntl_client_stop ===");
  ntl_running=false;
  mg_mgr_free(&ntl_mgr);
  log_close();
}

void ntl_client_poll(tenv* env) {
  if(!ntl_running) return;
  mg_mgr_poll(&ntl_mgr,0);
  double now=glfwGetTime();
  if(now>=next_poll_time){
    next_poll_time=now+NTL_POLL_INTERVAL;
    ntl_issue_request(env);
  }
}

ntl_player* ntl_get_players(int* count) {
  if(!ntl_running){*count=0;return NULL;}
  *count=ntl_player_count;
  return ntl_players_list;
}
