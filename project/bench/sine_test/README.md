# sine_test

輸出鏈路的對照組。板子自己產生 440Hz 正弦波，不碰輸入。

在「輸入全死、診斷指向 codec ADC」的當下，這支的用途是回答一個問題：
**DAC 這條還活著嗎？** 有嗶聲 = 輸出鏈路與喇叭端沒有在這期間壞掉，
in_probe 讀到的 `nz=0` 就確定只能是輸入側的問題。

用法：`make`，然後 `.claude/hooks/flash.sh`。
接線：pin18 (AUDIO OUT L) → Tip、pin20 (AGND) → Sleeve → EVO 4 Line In。
