# 開發歷史與未來方向

## 主要 Pull Request
- **#10 更新 README 說明 MQTT**：合併紀錄於 `2270718`，加入如何發佈感測數據至 HiveMQ【F:README.md†L27-L40】。
- **#7 Imgbot 圖片最佳化**：自動壓縮儲存庫內圖片，提升 repo 大小效率【F:logs/codex/1.png】等。
- **#6 新增血氧功能與 UART 整合**：整合 MAX30102 心率血氧模組，Arduino 透過 UART 傳送至 ESP32【F:sub_device/eee4464-uno/eee4464-uno.ino†L50-L110】。
- **#5 新增 HTTP 佇列測試**：加入 `test_http_queue.c` 驗證高優先序請求在佇列滿時仍能送出【F:tests/test_http_queue.c†L28-L65】。
- **#4 修正 Cloudflare API 日誌**：調整 `cloudflare_api` 函式的 log 訊息更正確【F:cloudflare_api/cloudflare_api.c†L130-L140】。
- **#3 Makefile run target 修正**：避免與 `build` 目錄衝突，新增 `compile` 目標【F:makefile†L21-L23】。
- **#2 README 環境敘述修正**：修正錯誤的環境變數名【commit a0a0e77】。

## 問題與解決方式
- 早期版本存在 DHT 時序錯誤與資料偏移，透過 PR #1 修正 `dht_read_raw` 讀取邏輯【F:components/dht/dht.c†L8-L32】。
- HTTP 佇列曾因空間不足造成資料遺失，後續加入高優先序處理及單元測試確保可靠度【F:main/main.c†L160-L213】【F:tests/test_http_queue.c†L28-L65】。
- Uno 端若在 PWM 腳位 3 或 11 使用 `analogWrite`，會與 IR 接收衝突；程式特別標註只能 `digitalWrite`【F:sub_device/eee4464-uno/eee4464-uno.ino†L229-L233】。

## 未來優化方向
- **模組化感測器程式**：拆分為多個 `sensor_xxx.c`，方便重用與單元測試。
- **加強錯誤重試與佇列監控**：目前僅在 HTTP 失敗時簡單重送，可再加入指標型重試策略與 WDT reset 保護。
- **探索完整 MQTT 架構**：若需更即時資料，可將雲端服務改以 MQTT 為主，並加入 TLS 驗證與配置檔。

