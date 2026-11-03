// in_probe - 把 in[0] 的實際數值印出來，並診斷 codec 初始化路徑
//
// 已知事實（2026-09-06 實測）：
//   blocks 每 500ms 約 501 次 = 1002 blocks/秒，48000/48 = 1000，音訊引擎正常。
//   nonzero = 0 / 24048 —— 每個 sample 都是「剛好 0.000000」。
//   -> SAI/DMA 在跑，但 codec 的 ADC 沒有送資料。
//
// ---------------------------------------------------------------------------
// 2026-09-08 重大修正：PB11 切換與 LED 從主迴圈搬進 audio callback
// ---------------------------------------------------------------------------
// 症狀：燒進去後 LED 恆亮、PB11 從沒翻過，等於整個診斷沒跑。
//
// 原因：hw.StartLog(false) 不等於「不阻塞」。看 logger.cpp:52
//         pc_sync_ = wait_for_pc ? LOGGER_SYNC_IN : LOGGER_SYNC_OUT;
//       pc_sync_ 從 0 起算，每次 Transmit() 成功就 ++（logger.cpp:81-84），
//       一到 LOGGER_SYNC_IN(=2) 就改走 TransmitSync()，那是無限忙等
//       （logger.h:111-113）。而 StartLog() 自己開頭就印兩行 banner，
//       剛好把 pc_sync_ 推到 2 —— 所以主迴圈的第一次 PrintLine 就卡死，
//       只要 PC 端沒有程式在讀那個 COM port。
//
// 修法：PB11 切換和 LED 都只是單次 GPIO 暫存器寫入，不配置記憶體、不阻塞，
//       放進 callback 是 RT 安全的。這樣就算主迴圈卡在 print，
//       核心診斷（PB11 交替 + 可聽的 passthrough + LED 簽名）照樣跑。
//       序列埠有接就是加分，沒接也不影響結論。
//
//       計時改用 block 計數而非 System::GetNow()：48kHz / 48 samples
//       = 每秒 1000 個 block，所以 5000 block = 5 秒，確定性且不碰任何
//       時間 API。
// ---------------------------------------------------------------------------
//
// 欄位（序列埠有接才看得到）：
//   PD3/PD4/PD5  版本辨識腳，0 = 被接地。
//                這版 libDaisy（2024-01-08）的 CheckBoardVersion() 只讀
//                PD3 和 PD4，從不讀 PD5（daisy_seed.cpp:356-378）；
//                而 datasheet p.13 說 Rev7 的辨識腳是 PD5。
//   ver          libDaisy 判定結果。0=DAISY_SEED/Rev4/AK4556
//                1=SEED_1_1/WM8731  2=SEED_2_DFM/PCM3060
//   pb11         PB11 現在被驅動成高還是低。
//                DAISY_SEED_2_DFM（PCM3060）要求拉低（disable deemphasis）；
//                DAISY_SEED（Rev4）走 Ak4556::Init()，該函式最後一行是
//                dsy_gpio_write(&reset, 1)，把這支腳收在 HIGH
//                （codec_ak4556.cpp）。兩條路的 SAI 設定完全相同，
//                差別只有這支腳——所以它是頭號嫌疑。
//   blk          callback 呼叫次數。不增加代表音訊引擎沒跑。
//   nz           不等於 0 的 sample 數 / 總數。**關鍵欄位。**
//   pk/mn/mx     峰值與有號極值，單位是百萬分之一滿刻度（ppm）。
//                滿刻度 1.0 = 1000000 ppm；10000 ppm = -40dBFS。
//
// 判讀：pb11=0 的那幾行 nz 若跳成非 0，就是找到修法了。
//       不接序列埠時：聲音每 5 秒有無交替 = 同一個結論。
//
// LED 識別簽名：短─長─暗暗暗（每格 500ms，4 秒一輪），
//               與 sine_test 的「短─短─長」可肉眼區分。
//
// 音訊照樣直通（pin17 沒接，左聲道扇出到兩個輸出）。

#include "daisy_seed.h"
#include <math.h>

using namespace daisy;

DaisySeed hw;

// PB11 要在 callback 裡驅動，所以放檔案層級。
GPIO g_pb11;

volatile uint32_t g_blocks  = 0;
volatile uint32_t g_samples = 0;
volatile uint32_t g_nonzero = 0;
volatile float    g_peak    = 0.f;
volatile float    g_min     = 0.f;
volatile float    g_max     = 0.f;
volatile bool     g_pb11_high = true;  // 只給 print 讀，真正的狀態在 callback

// 48kHz / 48 samples = 1000 blocks/秒。
static const uint32_t kBlocksPerSec  = 1000;
static const uint32_t kPb11PeriodBlk = 5 * kBlocksPerSec;   // 5 秒翻一次
static const uint32_t kLedSlotBlk    = kBlocksPerSec / 2;   // 每格 500ms

// 短─長─暗暗暗（8 格 = 4 秒）
static const uint8_t kLedSig[8] = {1, 0, 1, 1, 0, 0, 0, 0};

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    uint32_t nz = 0;
    float    pk = 0.f;
    float    mn = 0.f;
    float    mx = 0.f;

    for (size_t i = 0; i < size; i++)
    {
        const float x = in[0][i];

        // 「剛好 0」與「很小但非 0」是本程式要分辨的核心，刻意用精確比較。
        if (x != 0.f)
            nz++;

        const float a = fabsf(x);
        if (a > pk) pk = a;
        if (x < mn) mn = x;
        if (x > mx) mx = x;

        out[0][i] = x;
        out[1][i] = x;
    }

    g_blocks++;
    g_samples += size;
    g_nonzero += nz;

    if (pk > g_peak) g_peak = pk;
    if (mn < g_min)  g_min  = mn;
    if (mx > g_max)  g_max  = mx;

    // --- 以下都是單次 GPIO 暫存器寫入，無配置、無阻塞 ---

    // 自由計數，不歸零；用取模判斷，溢位（約 49 天）也不影響。
    static uint32_t tick      = 0;
    static bool     pb11_high = true;
    tick++;

    if (tick % kPb11PeriodBlk == 0)
    {
        pb11_high = !pb11_high;
        g_pb11.Write(pb11_high);
        g_pb11_high = pb11_high;

        // 視窗跟著相位一起歸零：否則 PrintLine 阻塞時一個視窗會橫跨
        // 多次翻轉，印出「當下的 pb11 + 混了兩種相位的 nz」，
        // 剛好打壞「pb11=0 的行 nz 非 0 就是答案」這條判讀規則。
        g_blocks = 0; g_samples = 0; g_nonzero = 0;
        g_peak = 0.f; g_min = 0.f; g_max = 0.f;
    }

    if (tick % kLedSlotBlk == 0)
    {
        const uint32_t slot = (tick / kLedSlotBlk) & 7;
        hw.SetLed(kLedSig[slot] != 0);
    }
}

static int ToPpm(float x)
{
    return (int)(x * 1000000.f);
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    // 用跟 CheckBoardVersion() 相同的手法讀版本腳：輸入 + 內部提升電阻，
    // 被接地的那支讀到 0（出處：daisy_seed.cpp:363-370）。
    GPIO pd3, pd4, pd5;
    pd3.Init(Pin(PORTD, 3), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    pd4.Init(Pin(PORTD, 4), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    pd5.Init(Pin(PORTD, 5), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    System::Delay(10);

    const int r3 = pd3.Read() ? 1 : 0;
    const int r4 = pd4.Read() ? 1 : 0;
    const int r5 = pd5.Read() ? 1 : 0;

    int ver;
    switch (hw.CheckBoardVersion())
    {
        case DaisySeed::BoardVersion::DAISY_SEED:       ver = 0; break;
        case DaisySeed::BoardVersion::DAISY_SEED_1_1:   ver = 1; break;
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: ver = 2; break;
        default:                                        ver = 9; break;
    }

    // PB11 交給我們自己驅動。Ak4556::Init() 已經把它設成輸出並拉高，
    // 所以從 HIGH 開始等於維持 libDaisy 留下的狀態，第一段是對照組。
    g_pb11.Init(Pin(PORTB, 11), GPIO::Mode::OUTPUT);
    g_pb11.Write(true);
    g_pb11_high = true;

    hw.StartAudio(AudioCallback);

    // StartLog 放在 StartAudio 之後：它內部會印兩行 banner，
    // 那兩行就足以把 logger 推進阻塞模式（見檔頭說明）。
    // 音訊與 PB11/LED 都已經在跑了，所以這裡就算卡住也不影響診斷。
    hw.StartLog(false);

    // 先等一個視窗再進迴圈，否則第一行會是 blk=0 nz=0/0 的空快照，
    // 事後才開序列埠的人會誤讀成「音訊引擎沒跑」。
    System::Delay(500);

    for (;;)
    {
        const uint32_t blocks  = g_blocks;
        const uint32_t samples = g_samples;
        const uint32_t nonzero = g_nonzero;
        const float    pk      = g_peak;
        const float    mn      = g_min;
        const float    mx      = g_max;
        const bool     pb11    = g_pb11_high;  // 與計數器同一批快照

        g_blocks  = 0;
        g_samples = 0;
        g_nonzero = 0;
        g_peak    = 0.f;
        g_min     = 0.f;
        g_max     = 0.f;

        // 板子資訊印在每一行，不是開機印一次——燒完重開、序列埠重連時
        // 才不會錯過。
        //
        // 注意：沒有 PC 端程式在讀 COM port 時，這一行會永久阻塞。
        // 那是可接受的，因為 PB11 / LED / 音訊全都在 callback 裡跑。
        hw.PrintLine("PD3=%d PD4=%d PD5=%d ver=%d pb11=%d "
                     "blk=%d nz=%d/%d pk=%d mn=%d mx=%d",
                     r3, r4, r5,
                     ver,
                     pb11 ? 1 : 0,
                     (int)blocks,
                     (int)nonzero,
                     (int)samples,
                     ToPpm(pk),
                     ToPpm(mn),
                     ToPpm(mx));

        System::Delay(500);
    }
}
