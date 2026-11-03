// delay - 回聲效果。B 階段之後的第一個真效果。
//
// 訊號鏈與 passthru 完全相同：
//   Bass -> MS-60B+（當 buffer）-> 6.35mm TS 母座 -> pin16 (AUDIO IN L)
//   pin18 -> Tip、pin20 -> Sleeve -> EVO 4 Line In
//   pin17 (AUDIO IN R) 沒接，所以只讀 in[0]，扇出到左右兩個輸出。
//
// === 要調延遲時間，只改 kDelayMs 一個數字，然後重新編譯燒錄 ===
//   陣列大小必須在編譯期決定，所以沒辦法在板子上即時轉旋鈕。
//   想要旋鈕的話要接一顆電位器到 ADC 腳，那是另一件事。

#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

// --- 唯一要動的數字 ---------------------------------------------------------
// 延遲時間（毫秒）。上限由 kBufSamples 決定，見下面的 static_assert。
static const size_t kDelayMs = 500;
// ---------------------------------------------------------------------------

// 48kHz 之下每毫秒 48 個 sample。
static const size_t kSamplesPerMs = 48;

// buffer 開 0.75 秒 = 36000 格 = 144000 bytes ≈ 141KB。
// 開得比預設延遲大，是為了讓 kDelayMs 有往上調的空間。
// 預設 .bss 落在 SRAM 區（512K，core/STM32H750IB_flash.lds:110-122），放得下。
// 要拉到 2 秒立體聲（768KB）才需要 DSY_SDRAM_BSS（src/dev/sdram.h:81）。
static const size_t kBufSamples = 36000;

// 留 2 格餘裕，不要用滿。原因：DelayLine::Read() 的插值會多讀一格
// （delayline.h:80-81），delay 設成 max_size-1 時第二抽頭正好繞回寫指標
// ——也就是即將被覆寫的最舊格。目前 frac_ 是 0 所以無害，但日後改用
// SetDelay(float) 做可變延遲時，那一格會每個 sample 混進高頻雜訊。
static const size_t kDelaySamples = kDelayMs * kSamplesPerMs;
static_assert(kDelaySamples <= kBufSamples - 2,
              "kDelayMs 超過 buffer 容量，請同步加大 kBufSamples");

// DelayLine 物件本身就含著整條陣列。必須放檔案層級（.bss），
// 放進 callback 或 main 的區域變數會直接把堆疊撐爆。
static DelayLine<float, kBufSamples> del;

// 回授量：每繞一圈乘 0.5，回聲每次小一半。
// 必須 < 1.0f —— 等於或大於 1 的話能量只進不出，幾秒內爆成滿刻度噪音。
static const float kFeedback = 0.5f;

// 回聲混多大聲。乾訊號維持 1.0 不動，這樣跟 passthru 是同一個基準音量。
//
// 為什麼是 0.45 而不是更大（rt-audio-review 2026-09-08 指出）：
//   回授迴路的穩態峰值上界是 輸入峰值 x 1/(1-kFeedback) = 2.0 倍，
//   所以 mixed 的上界是 輸入峰值 x (1 + 0.45*2.0) = 1.9 倍。
//   in_probe 實測輸入峰值約 0.455 滿刻度 -> 1.9 x 0.455 = 0.86，還有餘裕。
//   若設 0.6，上界會變 2.2 倍 = 1.0，撞到 f2s24 的硬 clamp
//   （daisy_core.h:146-151）而削波。那種破音跟「codec 輸入有問題」的破音
//   耳朵分不出來，會污染這次的判讀。確認乾淨後再往上加。
static const float kWet = 0.45f;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    for (size_t i = 0; i < size; i++)
    {
        const float dry = in[0][i];

        // 先讀再寫。Read() 取的是 kDelaySamples 個 sample 之前寫進去的值；
        // 順序反過來的話會讀到剛剛才寫的那一個，延遲就沒了。
        const float wet = del.Read();

        // 回聲之所以會重複好幾次，全在這一行：舊的回聲被重新錄進 buffer，
        // 每繞一圈衰減一次，直到掉到聽不見。
        del.Write(dry + wet * kFeedback);

        const float mixed = dry + wet * kWet;

        out[0][i] = mixed;
        out[1][i] = mixed;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    // Init() 會把整條 buffer 清成 0。不清的話開機瞬間會把上次殘留的
    // 記憶體內容當成音訊播出去——一聲爆音。
    del.Init();
    del.SetDelay(kDelaySamples);

    hw.StartAudio(AudioCallback);

    // LED 識別簽名：長─短─短（每格 250ms，1 秒一輪），
    // 跟 sine_test 的「短─短─長」和 in_probe 的「短─長─暗暗暗」都能分辨。
    // 這樣一眼就知道板子上跑的是哪支韌體。
    static const bool kLedSig[4] = {true, false, true, false};

    for (;;)
    {
        hw.SetLed(kLedSig[(System::GetNow() / 250) & 3]);

        // 節流，免得全速對 GPIO 寫入（rt-audio-review 建議）。
        // 在 main loop 呼叫 Delay 是安全的，不在 callback 裡。
        System::Delay(1);
    }
}
