#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "secrets.h"

// AI-Thinker Pin Map
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

#define STREAM_PORT 81
#define BOUNDARY    "frame"

static float    g_fps         = 0.0f;
static uint32_t g_frame_count = 0;
static uint32_t g_last_fps_ms = 0;
static httpd_handle_t g_httpd = NULL;

// ─────────────────────────────────────────────────────────
//  Dashboard HTML
// ─────────────────────────────────────────────────────────
static const char INDEX_HTML[] = R"rawhtml(
<!DOCTYPE html><html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-CAM Pro Console</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:#0d1117;color:#c9d1d9;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;display:flex;flex-direction:column;align-items:center;padding:20px 16px;}
  h1{font-size:15px;color:#8b949e;letter-spacing:3px;text-transform:uppercase;margin-bottom:18px;}
  .main{display:grid;grid-template-columns:1fr 280px;gap:18px;width:100%;max-width:960px;}
  .viewer{position:relative;background:#111;border-radius:12px;border:1px solid #21262d;overflow:hidden;aspect-ratio:4/3;}
  #stream{width:100%;height:100%;object-fit:contain;display:block;}
  .live-tag{position:absolute;top:12px;left:12px;background:rgba(0,0,0,.75);border:1px solid #30363d;padding:5px 14px;border-radius:20px;font-size:12px;font-weight:600;z-index:10;}
  .dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:#3fb950;margin-right:6px;animation:blink 1.4s infinite;}
  @keyframes blink{0%,100%{opacity:1;}50%{opacity:.2;}}
  .side{display:flex;flex-direction:column;gap:12px;}
  .card{background:#161b22;border:1px solid #21262d;border-radius:10px;padding:15px;}
  .card-label{font-size:10px;color:#8b949e;text-transform:uppercase;letter-spacing:.8px;margin-bottom:6px;}
  .card-value{font-size:24px;font-weight:700;color:#f0f6fc;}
  .card-unit{font-size:13px;color:#8b949e;margin-left:4px;font-weight:400;}
  .btn{display:block;width:100%;padding:11px;border-radius:8px;font-weight:700;font-size:13px;cursor:pointer;text-align:center;border:none;transition:.15s;}
  .btn-g{background:#238636;color:#fff;} .btn-g:hover{background:#2ea043;}
  .btn-d{background:#21262d;color:#c9d1d9;border:1px solid #30363d;margin-top:8px;} .btn-d:hover{background:#30363d;}
  .info-panel{width:100%;max-width:960px;margin-top:22px;background:#161b22;border:1px solid #21262d;border-radius:12px;overflow:hidden;}
  .info-hdr{background:#0d1117;border-bottom:1px solid #21262d;padding:12px 18px;font-size:11px;font-weight:700;color:#8b949e;text-transform:uppercase;letter-spacing:1px;}
  .info-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));}
  .info-item{padding:14px 18px;border-right:1px solid #21262d;border-bottom:1px solid #21262d;}
  .info-key{font-size:10px;color:#8b949e;text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px;}
  .info-val{font-size:14px;font-weight:600;color:#58a6ff;}
</style>
</head>
<body>
<h1>📡 ESP32-CAM Performance Console</h1>
<div class="main">
  <div class="viewer">
    <div class="live-tag"><span class="dot"></span><span id="tag">CONNECTING</span> | <span id="tf">0.0</span> FPS</div>
    <img id="stream" alt="Stream">
  </div>
  <div class="side">
    <div class="card"><div class="card-label">Real-time FPS</div><div class="card-value" id="fps">0.0</div></div>
    <div class="card"><div class="card-label">Signal (RSSI)</div><div class="card-value" id="rssi">--<span class="card-unit">dBm</span></div></div>
    <div class="card"><div class="card-label">Free Heap</div><div class="card-value" id="heap">--<span class="card-unit">KB</span></div></div>
    <div class="card"><div class="card-label">Uptime</div><div class="card-value" id="uptime">0<span class="card-unit">s</span></div></div>
    <button class="btn btn-g" onclick="location.reload()">🔄 Refresh</button>
    <button class="btn btn-d" onclick="window.open('/capture')">📸 Snapshot</button>
  </div>
</div>
<div class="info-panel">
  <div class="info-hdr">🔧 Hardware &amp; System Information</div>
  <div class="info-grid">
    <div class="info-item"><div class="info-key">Device</div><div class="info-val" id="i-chip">--</div></div>
    <div class="info-item"><div class="info-key">Camera</div><div class="info-val">OV2640</div></div>
    <div class="info-item"><div class="info-key">CPU Freq</div><div class="info-val" id="i-cpu">--</div></div>
    <div class="info-item"><div class="info-key">Flash Size</div><div class="info-val" id="i-flash">--</div></div>
    <div class="info-item"><div class="info-key">PSRAM</div><div class="info-val" id="i-psram">--</div></div>
    <div class="info-item"><div class="info-key">Resolution</div><div class="info-val" id="i-res">--</div></div>
    <div class="info-item"><div class="info-key">JPEG Quality</div><div class="info-val" id="i-quality">--</div></div>
    <div class="info-item"><div class="info-key">IP Address</div><div class="info-val" id="i-ip">--</div></div>
    <div class="info-item"><div class="info-key">MAC Address</div><div class="info-val" id="i-mac">--</div></div>
    <div class="info-item"><div class="info-key">WiFi SSID</div><div class="info-val" id="i-ssid">--</div></div>
    <div class="info-item"><div class="info-key">Min Free Heap</div><div class="info-val" id="i-mheap">--</div></div>
    <div class="info-item"><div class="info-key">Framework</div><div class="info-val">Arduino / ESP-IDF</div></div>
  </div>
</div>
<script>
  // ── 스트리밍 로드 (오류 시 3초 후 재시도) ──
  var streamSrc = 'http://' + location.hostname + ':81/';
  var img = document.getElementById('stream');
  function loadStream(){
    img.src = streamSrc + '?t=' + Date.now();
  }
  img.onerror = function(){ setTimeout(loadStream, 3000); };
  loadStream();

  // ── 대시보드 폴링 ──
  async function poll(){
    try{
      var d = await (await fetch('/status')).json();
      document.getElementById('fps').innerText    = d.fps.toFixed(1);
      document.getElementById('tf').innerText     = d.fps.toFixed(1);
      document.getElementById('rssi').innerHTML   = d.rssi+'<span class="card-unit"> dBm</span>';
      document.getElementById('heap').innerHTML   = Math.round(d.heap/1024)+'<span class="card-unit"> KB</span>';
      document.getElementById('uptime').innerHTML = d.uptime+'<span class="card-unit">s</span>';
      document.getElementById('tag').innerText    = 'LIVE';
      document.getElementById('i-chip').innerText  = d.chip||'ESP32';
      document.getElementById('i-cpu').innerText   = (d.cpu_mhz||'--')+' MHz';
      document.getElementById('i-flash').innerText = (d.flash_mb||'--')+' MB';
      document.getElementById('i-psram').innerText = d.psram_kb>0?d.psram_kb+' KB':'None';
      document.getElementById('i-res').innerText   = d.resolution||'--';
      document.getElementById('i-quality').innerText = d.quality||'--';
      document.getElementById('i-ip').innerText    = d.ip||'--';
      document.getElementById('i-mac').innerText   = d.mac||'--';
      document.getElementById('i-ssid').innerText  = d.ssid||'--';
      document.getElementById('i-mheap').innerText = Math.round((d.min_heap||0)/1024)+' KB';
    }catch(e){}
  }
  poll(); setInterval(poll, 1000);
</script>
</body></html>
)rawhtml";

// ─────────────────────────────────────────────────────────
//  Port 80 – HTTP Handlers
// ─────────────────────────────────────────────────────────
static esp_err_t index_handler(httpd_req_t *req){
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t status_handler(httpd_req_t *req){
    sensor_t *s = esp_camera_sensor_get();
    const char *resName = "QVGA(320x240)";
    if(s){
        switch(s->status.framesize){
            case FRAMESIZE_QQVGA: resName="QQVGA(160x120)"; break;
            case FRAMESIZE_VGA:   resName="VGA(640x480)";   break;
            default: break;
        }
    }
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"fps\":%.1f,\"rssi\":%d,\"heap\":%u,\"uptime\":%lu,"
        "\"chip\":\"ESP32\",\"cpu_mhz\":%d,\"flash_mb\":%lu,\"psram_kb\":%lu,"
        "\"resolution\":\"%s\",\"quality\":%d,\"ip\":\"%s\",\"mac\":\"%s\","
        "\"ssid\":\"%s\",\"min_heap\":%u}",
        g_fps,(int)WiFi.RSSI(),ESP.getFreeHeap(),(unsigned long)(millis()/1000),
        (int)getCpuFrequencyMhz(),
        (unsigned long)(ESP.getFlashChipSize()/1048576UL),
        (unsigned long)(ESP.getPsramSize()/1024UL),
        resName, s?s->status.quality:0,
        WiFi.localIP().toString().c_str(),
        WiFi.macAddress().c_str(),
        WiFi.SSID().c_str(),
        ESP.getMinFreeHeap());
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
    return httpd_resp_send(req,buf,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t *fb=esp_camera_fb_get();
    if(!fb) return httpd_resp_send_500(req);
    httpd_resp_set_type(req,"image/jpeg");
    esp_err_t res=httpd_resp_send(req,(const char*)fb->buf,fb->len);
    esp_camera_fb_return(fb); return res;
}

// ─────────────────────────────────────────────────────────
//  Port 81 – Raw TCP MJPEG Server (완전 독립, httpd 무관계)
// ─────────────────────────────────────────────────────────
static bool send_all(int sock, const uint8_t *buf, size_t len){
    size_t sent = 0;
    while(sent < len){
        int r = send(sock, buf+sent, len-sent, 0);
        if(r <= 0) return false;
        sent += r;
    }
    return true;
}

static void handle_stream_client(int sock){
    // HTTP 응답 헤더 전송
    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Connection: close\r\n"
        "\r\n";
    send(sock, hdr, strlen(hdr), 0);

    char part_buf[80];
    uint32_t fps_time  = millis();
    uint32_t fps_count = 0;

    while(true){
        camera_fb_t *fb = esp_camera_fb_get();
        if(!fb){ vTaskDelay(10/portTICK_PERIOD_MS); continue; }

        // MJPEG 파트 헤더
        int hlen = snprintf(part_buf, sizeof(part_buf),
            "--" BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n\r\n", fb->len);
        bool ok = send_all(sock, (uint8_t*)part_buf, hlen);
        if(ok) ok = send_all(sock, fb->buf, fb->len);
        if(ok) ok = send_all(sock, (uint8_t*)"\r\n", 2);

        esp_camera_fb_return(fb);
        if(!ok) break;

        fps_count++;
        uint32_t now = millis();
        if(now - fps_time >= 1000){
            g_fps      = (fps_count * 1000.0f) / (now - fps_time);
            fps_count  = 0;
            fps_time   = now;
        }
        vTaskDelay(1/portTICK_PERIOD_MS);
    }
    close(sock);
}

static void stream_server_task(void *arg){
    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(STREAM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 3);
    Serial.printf("[OK] Raw stream server on port %d\n", STREAM_PORT);

    while(true){
        int client = accept(listen_fd, NULL, NULL);
        if(client >= 0) handle_stream_client(client);
    }
}

// ─────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────
void setup(){
    Serial.begin(115200);

    camera_config_t cfg;
    cfg.ledc_channel=LEDC_CHANNEL_0; cfg.ledc_timer=LEDC_TIMER_0;
    cfg.pin_d0=Y2_GPIO_NUM; cfg.pin_d1=Y3_GPIO_NUM;
    cfg.pin_d2=Y4_GPIO_NUM; cfg.pin_d3=Y5_GPIO_NUM;
    cfg.pin_d4=Y6_GPIO_NUM; cfg.pin_d5=Y7_GPIO_NUM;
    cfg.pin_d6=Y8_GPIO_NUM; cfg.pin_d7=Y9_GPIO_NUM;
    cfg.pin_xclk=XCLK_GPIO_NUM; cfg.pin_pclk=PCLK_GPIO_NUM;
    cfg.pin_vsync=VSYNC_GPIO_NUM; cfg.pin_href=HREF_GPIO_NUM;
    cfg.pin_sccb_sda=SIOD_GPIO_NUM; cfg.pin_sccb_scl=SIOC_GPIO_NUM;
    cfg.pin_pwdn=PWDN_GPIO_NUM;   cfg.pin_reset=RESET_GPIO_NUM;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 30;
    cfg.fb_count     = psramFound() ? 3 : 1;

    if(esp_camera_init(&cfg) != ESP_OK){
        Serial.println("[ERROR] Camera init failed"); return;
    }
    Serial.printf("[OK] PSRAM:%s\n", psramFound()?"Found":"None");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while(WiFi.status() != WL_CONNECTED){ delay(500); Serial.print("."); }
    Serial.printf("\nDashboard : http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Stream    : http://%s:%d\n",
        WiFi.localIP().toString().c_str(), STREAM_PORT);

    // ── Port 80: 대시보드 ──
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_open_sockets = 5;
    http_cfg.lru_purge_enable = true;
    if(httpd_start(&g_httpd, &http_cfg) == ESP_OK){
        httpd_uri_t u1={"/",        HTTP_GET,index_handler,   NULL};
        httpd_uri_t u2={"/status",  HTTP_GET,status_handler,  NULL};
        httpd_uri_t u3={"/capture", HTTP_GET,capture_handler, NULL};
        httpd_register_uri_handler(g_httpd, &u1);
        httpd_register_uri_handler(g_httpd, &u2);
        httpd_register_uri_handler(g_httpd, &u3);
        Serial.println("[OK] HTTP server on port 80");
    }

    // ── Port 81: Raw TCP 스트리밍 (Core 1) ──
    xTaskCreatePinnedToCore(
        stream_server_task, "stream_srv",
        8192, NULL, 5, NULL, 1);
}

void loop(){ vTaskDelay(1000/portTICK_PERIOD_MS); }
