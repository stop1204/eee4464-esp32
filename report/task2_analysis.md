# 程式深度分析

## 1. 感測器初始化與讀取方式
- **RCWL-0516**：在 `setup_rcwl0516_sensor()` 中將腳位設定為輸入【F:main/main.c†L640-L651】。
- **土壤濕度與水泵**：`init()` 內配置 ADC 及繼電器腳位【F:main/main.c†L699-L705】；讀取以 `read_soil_sensor()` 取多次資料求中位數。【F:main/main.c†L88-L120】
- **ACS712**：設定 ADC 通道後，以 64 次取樣平均換算電流。【F:main/main.c†L1043-L1056】
- **光敏電阻**：同樣使用 ADC 讀取並計算電壓。【F:main/main.c†L1003-L1006】
- **DHT11**：透過自訂 `dht_read_float_data()` 取得溫濕度，再進行校正【F:main/main.c†L1066-L1071】。
- **心率感測**：在 Arduino 程式中以 MAX30102 量測並於 `sendDataToESP32()` 經 UART 傳送【F:sub_device/eee4464-uno/eee4464-uno.ino†L50-L56】；ESP32 端 `process_arduino_data()` 解析 JSON【F:main/main.c†L1174-L1194】。

## 2. 任務分配與 FreeRTOS
- 程式於 `app_main()` 建立多個任務：HTTP 處理、UART 事件、`main_loop_task` 與 `second_loop_task` 等【F:main/main.c†L1231-L1243】。
- 任務之間透過佇列 (`http_request_queue`) 交換資料，避免在中斷或其他任務中直接進行網路操作，符合 FreeRTOS 非阻塞原則。
- 使用 `xTaskCreatePinnedToCore` 分配核心，並在時間敏感處適當 `vTaskDelay` 避免佔用 CPU。【F:main/main.c†L1235-L1243】

## 3. 為何採用 HTTP 而非 MQTT
- 程式以 Cloudflare Worker 提供的 REST API 上傳裝置及感測值，HTTP 較易處理佇列與重送機制，在缺乏持續連線時也較穩定。唯電流數據仍透過 MQTT 發送即時資訊。【F:main/main.c†L962-L971】【F:main/main.c†L1051-L1052】
- 若追求即時或大量資料傳輸，可考慮將其他感測值也改以 MQTT 發佈，或使用 MQTT QoS 1/2 保證送達。

## 4. 潛在阻塞與資源問題
- 在 `http_request_task()` 中使用 `xQueueReceive` 最長等待 5 秒【F:main/main.c†L171-L213】，如佇列長期無資料可能導致此任務阻塞，但整體仍在單獨任務中執行，不影響其他功能。
- `cloudflare_*` 函式皆有重送與 timeout 設定【F:cloudflare_api/cloudflare_api.c†L58-L140】，減少因網路失敗造成的卡住，但仍須留意記憶體配置與釋放。
- 內部未見明顯記憶體洩漏，但佇列過滿時可能丟棄資料，建議監控 `uxQueueSpacesAvailable()` 的結果。

## 5. 結構與錯誤處理建議
- 建議將各感測器讀取獨立為模組，例如 `sensor_dht.c`、`sensor_soil.c` 等，便於維護與測試。
- `process_arduino_data()`、`handle_cloud_controls()` 等可增加錯誤檢查和 log 等級區分，方便除錯。
- 建立統一的 log 宏或 wrapper，可依需要切換到不同 log 等級。

## 技術總結
此專案結合 ESP-IDF 與 Arduino，採用多任務架構；感測資料多透過 HTTP REST API 上傳，適合簡易雲端介接。需特別注意感測器校正與佇列容量，亦可視需求增加 MQTT 支援或改進模組化結構，提升可維護性與效能。
