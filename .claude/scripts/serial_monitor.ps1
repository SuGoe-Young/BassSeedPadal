# serial_monitor.ps1 - 即時看 Daisy 從 USB 序列埠印出來的東西（2026-11-04）
#
# 用法（在專案根目錄）：
#   powershell -ExecutionPolicy Bypass -File .claude/scripts/serial_monitor.ps1
#   powershell -ExecutionPolicy Bypass -File .claude/scripts/serial_monitor.ps1 -Port COM5
# 按 Ctrl+C 結束。
#
# 看到 "k1=… k2=… k3=…"（knob_test 的格式）會畫成長條，其他內容原樣印出。
# 注意：同一時間只能有一個程式開著這個 COM port。這個開著時，Claude 就讀不到。

param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$Seconds = 0   # 0 = 一直跑到按 Ctrl+C；給數字就跑那麼多秒（測試用）
)

# 沒指定 port 就找 STM32 的 USB 序列裝置（VID 0483），找不到再用 COM3
if (-not $Port) {
    $dev = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -match "VID_0483" -and $_.Name -match "\((COM\d+)\)" } |
        Select-Object -First 1
    if ($dev -and $dev.Name -match "\((COM\d+)\)") { $Port = $Matches[1] } else { $Port = "COM3" }
}

function Bar([int]$v) {
    $n = [math]::Round([math]::Max(0, [math]::Min(1000, $v)) / 50)   # 0~20 格
    return ("█" * $n) + ("░" * (20 - $n)) + (" {0,4}" -f $v)
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud
$sp.ReadTimeout = 1000
$sp.DtrEnable = $true
try {
    $sp.Open()
} catch {
    Write-Host "打不開 $Port：$($_.Exception.Message)"
    Write-Host "檢查：板子有接 USB 嗎？有沒有別的程式（或 Claude）正開著這個 port？"
    exit 1
}

Write-Host "已連上 $Port（Ctrl+C 結束）"
$end = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }
try {
    while ((Get-Date) -lt $end) {
        try { $line = $sp.ReadLine().Trim() } catch [System.TimeoutException] { continue }
        if ($line -match "k1=(-?\d+)\s+k2=(-?\d+)\s+k3=(-?\d+)") {
            Write-Host ("旋鈕1 {0}   旋鈕2 {1}   旋鈕3 {2}" -f (Bar $Matches[1]), (Bar $Matches[2]), (Bar $Matches[3]))
        } elseif ($line) {
            Write-Host $line
        }
    }
} finally {
    $sp.Close()
}
