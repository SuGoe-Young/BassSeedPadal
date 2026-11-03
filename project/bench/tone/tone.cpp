// tone - 第四個效果：tone 旋鈕（低通濾波器），自己轉（2026-10-03）
//
// 比喻：開車經過有大山坡和小碎石的路。
//   路面 x = 原本的聲音；車身 y = 濾波後的聲音。
//   車身有避震，每一瞬間只往路面移一小段，所以跟得上大山坡（低頻），跟不上小碎石（高頻）。
//   避震越軟（截止頻率越低），聲音越悶；越硬（越高），越亮。
//
// 這支在聽什麼：
//   還沒有實體旋鈕，所以讓板子自己轉：截止頻率每 8 秒從 200 Hz 掃到 5000 Hz 再掃回來。
//   你會聽到假貝斯慢慢從「悶在被子裡」變成「很亮、有弦的顆粒」，再變回去。
//   LED 亮 = 正在往亮的方向轉；LED 暗 = 往悶的方向轉。
//
// 輸入：假貝斯（力道不均那版），內建低通調到 8000 Hz 讓原音夠亮，濾波器才有東西可以削。

#include "daisy_seed.h"
#include "daisysp.h"
#include "../../common/demo_bass.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static DemoBass demo;

// --- 輸入來源 ---------------------------------------------------------------
static const bool kUseDemoInput = true;

// --- 掃頻範圍 ---------------------------------------------------------------
static const float kFcLow   = 200.f;  // 最悶
static const float kFcHigh  = 5000.f; // 最亮
static const float kSweepSec = 8.f;   // 悶 → 亮 → 悶 一趟

// 過幾層避震。1 層的削法很溫和（越遠才削越多），掃起來不明顯；
// 2 層 = 同一台車的避震再過一次，削得更乾脆，耳朵比較聽得出來。
static const int kStages = 2;

// 車身位置（每一層一個）。要從上一個 sample 延續下去，所以放在函式外。
static float y[kStages] = {0.f, 0.f};

static float           sr         = 48000.f;
static float           sweep_pos  = 0.f;   // 0 ~ 1，一趟走到哪
static float           sweep_step = 0.f;   // 每個 block 前進多少
static volatile bool   going_up   = true;  // 給 LED 看

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // ── 每個 block（48 個 sample = 1 ms）算一次避震硬度 ──
    // 旋鈕不需要每個 sample 更新：人轉旋鈕的速度比 1 ms 慢太多了。
    // 這叫「控制速率」：聲音每秒 48000 次，控制每秒 1000 次。
    // 所以這裡用 powf / expf 是可以的——每秒 1000 次，不是 48000 次。
    sweep_pos += sweep_step;
    if(sweep_pos >= 1.f)
        sweep_pos -= 1.f;
    going_up = sweep_pos < 0.5f;
    const float tri = going_up ? 2.f * sweep_pos : 2.f - 2.f * sweep_pos; // 0→1→0

    // 用「倍數」掃，不是用「加法」掃：耳朵聽頻率是看倍數（200→400 和 2000→4000 聽起來一樣遠）。
    const float fc = kFcLow * powf(kFcHigh / kFcLow, tri);

    // 避震硬度 a：截止頻率越高，a 越大，車身追得越緊。
    const float a = 1.f - expf(-2.f * 3.14159265f * fc / sr);

    // ── 每個 sample：車身往路面移一小段 ──
    for(size_t i = 0; i < size; i++)
    {
        float x = kUseDemoInput ? demo.Process() : in[0][i];

        for(int s = 0; s < kStages; s++)
        {
            y[s] += a * (x - y[s]); // 就是這一行
            x = y[s];               // 下一層避震吃的是這一層的車身
        }

        out[0][i] = x;
        out[1][i] = x;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    sr = hw.AudioSampleRate();

    demo.Init(sr);
    demo.SetToneHz(8000.f);

    // 每個 block 前進「block 長度 / 一趟長度」。block 48、8 秒時一趟 8000 個 block。
    // 用 AudioBlockSize() 而不寫死 48：之後改 block size，掃頻速度才不會跟著錯。
    sweep_step = (float)hw.AudioBlockSize() / (kSweepSec * sr);

    hw.StartAudio(AudioCallback);

    // LED：往亮轉時亮、往悶轉時暗，4 秒 / 4 秒。跟其他 bench 的節奏都不同。
    for(;;)
    {
        hw.SetLed(going_up);
        System::Delay(1);
    }
}
