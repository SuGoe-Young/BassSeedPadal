---
name: daisy-flash
description: Flash the active Daisy Seed project to the board over DFU. Use when the user wants to upload, flash, burn, or program firmware onto the Daisy Seed ("燒錄", "flash it", "上傳到板子", "program-dfu"), or when a build succeeded and they want to hear or see it running on hardware.
---

# 燒錄到 Daisy Seed

## 關鍵事實（不知道這兩件事一定會誤判）

**1. 成敗只能看輸出字串，不能看 exit code。**
`make program-dfu` 成功時也會回 **exit 74**。因為 Makefile 帶了 `:leave`，
晶片寫完立刻重啟，dfu-util 最後一次 `get_status` 拿不到回應就報錯。
唯一可靠的成功訊號是輸出裡有 `File downloaded successfully`。

**2. 進 DFU 模式要實體按鍵，Claude 做不到。**
必須請使用者操作，然後等他回覆再繼續。

## 步驟

### 1. 先確認編譯過了

Stop hook 會自動編 `active_project`。如果這一輪剛改過程式碼但還沒經過 hook，
先手動編一次：

```bash
export PATH="/c/Program Files/DaisyToolchain/bin:$PATH"
cd "$(cat .claude/active_project)" && make
```

燒舊的 binary 是最容易浪費時間的錯誤——使用者會以為改動沒效果。

### 2. 請使用者把板子推進 DFU 模式

原話轉達這個順序，**順序錯了不會進 DFU**：

> 按住 BOOT → 點一下 RESET → 放開 RESET → 放開 BOOT

然後停下來等使用者回覆。不要自己往下跑。

### 3. 燒

```bash
./.claude/hooks/flash.sh
```

腳本已經處理好：補 PATH、先用 `dfu-util -l` 確認裝置在、跑 `make program-dfu`、
用 `File downloaded successfully` 判定成敗。成功 exit 0，失敗 exit 1。

### 4. 判讀結果

| 輸出 | 意思 | 怎麼辦 |
|---|---|---|
| `flash: OK (...)` | 成功，晶片已重啟在跑新韌體 | 請使用者確認實際行為 |
| `flash: 沒偵測到 DFU 裝置` | 沒進 DFU，或 driver 沒裝 | 重做步驟 2；仍失敗就查 Zadig |
| `dfu-util: Cannot open DFU device` | driver 問題 | 用 `tools/zadig-2.9.exe` 把 DFU 裝置的 driver 換成 WinUSB |
| 有 `File downloaded successfully` 但腳本說失敗 | 不該發生，回報異常 | 貼完整輸出給使用者 |

**不要**因為 exit code 非 0 就宣告失敗——先讀輸出。

## 備案

腳本一直不行的話，還有網頁燒錄器：<https://flash.daisy.audio/>
（要燒 `build/<name>.bin`）。這條路不需要本機 dfu-util，可以用來區分
「板子/driver 有問題」還是「toolchain 有問題」。
