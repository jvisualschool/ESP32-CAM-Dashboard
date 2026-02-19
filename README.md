# 📡 ESP32-CAM Performance Console

> AI Thinker ESP32-CAM 모듈을 활용한 **실시간 MJPEG 스트리밍 + 시스템 대시보드** 프로젝트

---

## 📸 미리보기

| 기능 | 설명 |
|------|------|
| 🎥 실시간 스트리밍 | QVGA(320×240) MJPEG, 목표 ~10 FPS |
| 📊 실시간 대시보드 | FPS, 신호세기, 여유 메모리, 업타임 |
| 🔧 장비 정보 패널 | 칩, CPU, Flash, PSRAM, IP/MAC 등 |
| 📸 정지 화면 캡처 | `/capture` 엔드포인트로 JPEG 다운로드 |

---

## 🛠 사용 하드웨어

| 항목 | 사양 |
|------|------|
| **보드** | AI Thinker ESP32-CAM |
| **MCU** | ESP32-D0WD (Dual Core, 240MHz) |
| **카메라** | OV2640 (최대 2MP) |
| **Flash** | 4MB SPI Flash |
| **PSRAM** | 4MB PSRAM (고속 프레임 버퍼 사용) |
| **WiFi** | 802.11 b/g/n (2.4GHz) |
| **USB-UART** | FTDI / CH340 업로드 어댑터 필요 |

---

## 📦 소프트웨어 & 의존성

| 항목 | 버전 |
|------|------|
| Framework | Arduino for ESP32 |
| Build Tool | PlatformIO |
| Platform | espressif32 |
| Board | `esp32cam` |
| Camera Library | `esp_camera.h` (ESP-IDF 내장) |
| HTTP Server | `esp_http_server.h` (ESP-IDF 내장) |

---

## 📁 프로젝트 구조

```
74_ESP32-CAM/
├── src/
│   └── esp32cam.cpp        # 메인 소스 (카메라, 서버, 스트리밍)
├── include/
│   └── secrets.h           # WiFi 인증 정보 (SSID / PASSWORD)
├── platformio.ini          # PlatformIO 빌드 설정
└── README.md
```

---

## ⚙️ 카메라 핀 설정 (AI Thinker)

| 신호 | GPIO |
|------|------|
| PWDN | 32 |
| XCLK | 0 |
| SIOD | 26 |
| SIOC | 27 |
| Y2~Y9 | 5, 18, 19, 21, 36, 39, 34, 35 |
| VSYNC | 25 |
| HREF | 23 |
| PCLK | 22 |

---

## 🔑 WiFi 설정

`include/secrets.h` 파일에 아래 내용을 작성합니다:

```cpp
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
```

> ⚠️ `secrets.h` 는 `.gitignore`에 추가하여 공개 저장소에 업로드하지 마세요.

---

## 🚀 빌드 & 업로드

### 1. 사전 준비

- [PlatformIO](https://platformio.org/) 설치 (VS Code 확장 또는 CLI)
- USB-UART 어댑터 연결 후 ESP32-CAM의 `IO0` 핀을 `GND`에 연결 (업로드 모드 진입)

### 2. `platformio.ini` 포트 설정

```ini
upload_port = /dev/cu.usbserial-210   ; macOS 예시
monitor_port = /dev/cu.usbserial-210
```

> Windows의 경우 `COM3` 등으로 변경

### 3. 빌드 & 업로드

```bash
# 빌드 + 업로드
pio run -t upload

# 시리얼 모니터 (업로드 후 IO0 핀 GND 해제)
pio device monitor -b 115200
```

---

## 🌐 웹 인터페이스

ESP32가 WiFi에 연결되면 시리얼 모니터에 IP 주소가 출력됩니다:

```
http://192.168.0.126
```

### API 엔드포인트

| 경로 | 설명 |
|------|------|
| `GET /` | 대시보드 웹 페이지 |
| `GET /stream` | MJPEG 실시간 스트리밍 |
| `GET /status` | 시스템 상태 JSON |
| `GET /capture` | JPEG 정지 이미지 |

### `/status` 응답 예시

```json
{
  "fps": 9.8,
  "rssi": -44,
  "heap": 163840,
  "uptime": 138,
  "chip": "ESP32",
  "cpu_mhz": 240,
  "flash_mb": 4,
  "psram_kb": 4096,
  "resolution": "QVGA(320x240)",
  "quality": 30,
  "ip": "192.168.0.126",
  "mac": "E0:8C:FE:B6:1A:50",
  "ssid": "MyWiFi",
  "min_heap": 155648
}
```

---

## 📐 카메라 설정 (성능 튜닝)

현재 설정 (`PSRAM 있는 경우`):

```cpp
cfg.frame_size   = FRAMESIZE_QVGA;  // 320 x 240
cfg.jpeg_quality = 30;              // 1(최고화질)~63(최저화질) — 높을수록 파일 작아 FPS ↑
cfg.fb_count     = 3;               // 트리플 버퍼링으로 연속 캡처 최적화
```

### FPS vs 화질 가이드

| 해상도 | JPEG Quality | 예상 FPS | 비고 |
|--------|-------------|---------|------|
| QQVGA (160×120) | 25 | ~15 FPS | 고속, 화질 낮음 |
| QVGA (320×240) | 30 | **~10 FPS** ← 현재 | 균형 |
| QVGA (320×240) | 12 | ~2 FPS | 고화질, 느림 |
| VGA (640×480) | 35 | ~3 FPS | 고해상도 |

---

## 🏗 아키텍처

```
┌─── HTTP Server (Port 80) ────────────────────────┐
│  GET /           → HTML 대시보드 반환             │
│  GET /status     → JSON 시스템 정보 반환          │
│  GET /capture    → JPEG 정지 이미지 반환          │
│  GET /stream     → 소켓 FD만 빼고 즉시 반환       │
└──────────────────────────────────────────────────┘
            │ httpd_req_to_sockfd()
            ▼
┌─── FreeRTOS Stream Task (Core 1) ────────────────┐
│  while(true):                                    │
│    esp_camera_fb_get() → JPEG 캡처               │
│    send() [블로킹] → 클라이언트로 전송            │
│    FPS 집계 (1초마다 갱신)                        │
└──────────────────────────────────────────────────┘
```

> MJPEG 스트리밍을 **별도 FreeRTOS 태스크**로 분리하여 httpd 워커가 `/status` 등 다른 요청을 동시에 처리할 수 있도록 설계

---

## 🐛 문제 해결

| 증상 | 원인 | 해결 |
|------|------|------|
| 업로드 안 됨 | IO0 핀이 GND에 연결 안 됨 | IO0–GND 연결 후 재시도 |
| 영상 안 나옴 | 포트 차단 또는 서버 과부하 | 브라우저 새로고침, IP 확인 |
| FPS 매우 낮음 | 화질 설정이 높거나 WiFi 신호 약함 | `jpeg_quality` 값 올리기, AP 가까이 이동 |
| 대시보드 수치 0 | httpd 워커 점유 | 현재 아키텍처(별도 태스크)로 해결됨 |
| 카메라 초기화 실패 | 핀 오결선 또는 전원 불안정 | 5V 전원 확인, 핀 재확인 |

---

## 📄 라이선스

MIT License — 자유롭게 사용, 수정, 배포 가능합니다.

---

*Built with ❤️ using ESP32-CAM + PlatformIO + Arduino Framework*
