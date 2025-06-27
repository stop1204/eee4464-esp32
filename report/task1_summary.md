# ESP32 Sensor Hub 程式概要

此專案以 ESP-IDF 為核心，ESP32 負責感測與網路連線，Arduino Uno 則透過 UART 回傳心率資料。主要模組與功能如下：

## 感測器與對應程式
- **DHT11** – 取得溫溼度，位於 `main/main.c` 中 `dht_read_float_data()` 呼叫【F:main/main.c†L1066-L1084】。
- **土壤濕度感測** – 透過 ADC 讀取並控制水泵繼電器，相關邏輯在 `read_soil_sensor()` 及後續控制判斷【F:main/main.c†L1090-L1159】。
- **ACS712 電流感測** – 讀取平均電壓換算電流，並以 MQTT 發佈【F:main/main.c†L1043-L1056】【F:main/main.c†L1051-L1052】。
- **光敏電阻 (LDR)** – 讀取光線值並轉為電壓【F:main/main.c†L1003-L1010】。
- **RCWL-0516 微波雷達** – 連續讀取五次決定是否有動作【F:main/main.c†L1016-L1035】。
- **心率感測** – Arduino Uno 利用 MAX30102 量測後以 UART 傳送 JSON，ESP32 在 `process_arduino_data()` 中解析【F:main/main.c†L1174-L1194】。

## 資料傳輸與任務架構
- HTTP 請求以佇列 `http_request_queue` 排序，由 `http_request_task()` 處理，上傳至 Cloudflare API【F:main/main.c†L168-L213】。
- MQTT 客戶端在 `second_loop_task()` 初始化，用於即時上傳電流資料等【F:main/main.c†L960-L971】【F:main/main.c†L1051-L1052】。
- 主要任務：
  - `main_loop_task()`：等待 Wi-Fi 連線並處理雲端控制【F:main/main.c†L864-L933】。
  - `second_loop_task()`：週期性讀取感測器並送出資料【F:main/main.c†L958-L1171】。
  - `button_task()`：處理測試按鈕中斷與佇列送出【F:main/main.c†L746-L809】。

## ESP‑IDF 與 Arduino
- ESP32 全部以 ESP‑IDF 實作，包含 FreeRTOS 任務與網路堆疊。
- Arduino Uno 採用 Arduino 框架撰寫 `eee4464-uno.ino`，負責心率量測並控制 LED。因 IR 接收佔用 Timer2，故部分 PWM 腳位僅能 `digitalWrite`【F:sub_device/eee4464-uno/eee4464-uno.ino†L229-L233】。

