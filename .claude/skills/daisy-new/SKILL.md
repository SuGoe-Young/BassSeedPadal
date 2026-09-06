---
name: daisy-new
description: Scaffold a new Daisy Seed project - creates the directory, a Makefile with correct library paths, an audio-passthrough starter .cpp, and points .claude/active_project at it so the build hook compiles it. Use when the user wants to start a new effect, synth, or experiment on the Daisy Seed ("開一個新專案", "new daisy project", "做一個 delay/reverb/oscillator"), rather than editing a stock DaisyExamples example.
---

# 建立新的 Daisy Seed 專案

## 什麼時候用

使用者要寫**自己的**東西（效果器、合成器、實驗），不是改官方範例的時候。
如果只是要跑一個現成範例，不要用這個 skill——直接改 `.claude/active_project`
指過去就好。

## 步驟

### 1. 確認名字與位置

專案名只能用英數字、底線、減號。預設放在 `DaisyExamples/seed/<name>`。
使用者沒指定位置就用預設，不要問。

### 2. 跑 scaffold 腳本

```bash
CLAUDE_PROJECT_DIR="$PWD" ./.claude/scripts/new_project.sh <name> [parent-dir]
```

腳本會做四件事：
- 建目錄
- 寫 `Makefile`（`LIBDAISY_DIR` / `DAISYSP_DIR` 用 `realpath --relative-to`
  依實際深度算出來，所以專案放在 repo 底下任何位置都對，不限官方範例的兩層）
- 寫 `<name>.cpp`：stereo audio passthrough 起手式，已經是能編能燒的完整程式
- 把 `.claude/active_project` 指向新專案

目錄已存在時腳本會拒絕覆寫並 exit 1。這是刻意的——不要用 `rm -rf` 繞過，
先問使用者。

### 3. 寫實際的 DSP

起手式只有 passthrough。接著在 `AudioCallback` 裡寫真正的處理。
慣例（跟官方範例一致，見 `DaisyExamples/seed/Osc/Osc.cpp`）：

- 硬體物件叫 `hardware`
- 用 `AudioHandle::InterleavingInputBuffer` / `InterleavingOutputBuffer`
- interleaved stereo：迴圈 `i += 2`，`out[i]` 是左、`out[i+1]` 是右
- sample rate 一律從 `hardware.AudioSampleRate()` 拿，**Init() 之後**才有效
- DaisySP 的物件都要 `.Init(samplerate)`

不確定某個 libDaisy / DaisySP 的 API 怎麼用時，**不要自己去翻 153 個 header**
——派 `daisy-api` subagent 查，只帶簽名和用法回來。

### 4. 編譯

Stop hook（`.claude/hooks/build.sh`）會在你這一輪結束時自動編 `active_project`，
編不過會 exit 2 擋下來並把錯誤丟回給你。所以**不需要**自己多跑一次 `make`。

要手動編的話記得 Git Bash 的 PATH 沒有 make：

```bash
export PATH="/c/Program Files/DaisyToolchain/bin:$PATH"
```

### 5. 燒錄

用 `daisy-flash` skill。燒錄需要使用者實體按 BOOT/RESET，不能自己完成。

## 驗收

- `<name>.cpp` 和 `Makefile` 都存在
- `.claude/active_project` 內容是 `<parent-dir>/<name>`
- 編譯通過（看 hook 有沒有擋）
