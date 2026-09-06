# daisy_seed 專案守則

## 開工前先讀

`me_and_claude_cowork_status/TASKS.md` — 目前做到哪、卡在哪的**單一真實來源**。
不要靠對話紀錄推測進度，讀這個檔。

同資料夾還有 `open_questions.md`（等 Shawn 回答的事）和
`reports/`（subagent 的完整回報原文）。

## 隨做隨寫

- 完成一個階段 → 立刻更新 `TASKS.md`，並在 `note/develop_log.md` 追加一段。
- **派 subagent 回來的回報，完整原文落檔到
  `me_and_claude_cowork_status/reports/YYYY-MM-DD-<主題>.md`**，
  然後在對話裡轉述重點。原因：subagent 的回報只回到 Claude 這裡，
  Shawn 在畫面上看不到，轉述又會濃縮。
- 新問題冒出來 → 寫進 `open_questions.md`，不要只留在對話裡。

## 這個專案的技術鐵則

- **Git Bash 的 PATH 沒有 make。** 手動編譯前要
  `export PATH="/c/Program Files/DaisyToolchain/bin:$PATH"`。
  （`build.sh` / `flash.sh` 裡已經補過，hook 不受影響。）
- **不要用 exit code 判斷燒錄成敗。** `make program-dfu` 成功時也會回 exit 74。
  只看輸出有沒有 `File downloaded successfully`。
- **不確定 libDaisy / DaisySP 的 API 就派 `daisy-api` subagent 查**，
  不要自己翻那 153 個 header，也不要憑記憶寫。
- **硬體相關的事實（接腳、電壓、驅動能力）必須有出處。**
  查不到就明說「未經證實」，不要混進已證實的事實裡——接錯線有硬體代價。
- 寫完 / 大改 `AudioCallback` 後，燒錄前派 `rt-audio-review` 審一次。
- 新加的 skill / agent 在**當前 session 讀不到**，要重開 Claude Code。
