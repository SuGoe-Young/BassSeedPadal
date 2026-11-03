// knob_test - 驗證 3 顆電位器的接線（2026-11-04）
//
// 這支只驗一件事：三顆旋鈕接對了沒、讀得到 0 ~ 1。
// 每顆旋鈕都有一個「聽得到 / 看得到」的結果，不用接序列埠也能判斷：
//   旋鈕 1（pin 22, A0）→ tone：假貝斯從悶（200 Hz）到亮（5000 Hz）
//   旋鈕 2（pin 23, A1）→ 音量：0 到原本大小
//   旋鈕 3（pin 24, A2）→ LED 閃爍速度：慢（1 秒）到快（0.05 秒）
// 另外每 0.2 秒從 USB 序列埠印出三個數值（0 ~ 1000），Claude 可以直接讀。
//
// 接線（出處：reports/2026-11-04-pots-adc-api.md）：
//   每顆電位器：一側 → pin 21（3V3 Analog），另一側 → pin 20（AGND），中間 → ADC 腳。
//   中間腳只能接 ADC 腳。中間腳若接到 3.3V 或地，轉到底時會讓 3.3V 直接短路到地。
//
// 序列埠注意（in_probe 踩過的坑）：StartLog(false) 之後，PC 沒在讀序列埠時
//   主迴圈的 PrintLine 會卡住。所以聲音、旋鈕、LED 全部放在 callback，
//   主迴圈只負責印字，卡住也不影響測試。

#include "daisy_seed.h"
#include "daisysp.h"
#include "../../common/demo_bass.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static DemoBass demo;

static const int kNumKnobs = 3;

// 旋鈕值（0 ~ 1），callback 每個 block 更新、main 讀來印。
static volatile float knob[kNumKnobs] = {0.f, 0.f, 0.f};

// 旋鈕平滑：ADC 讀值會有一點點雜訊，直接拿去控制 tone 會聽到細碎的沙沙聲。
// 每個 block（1 ms）做一次一階低通，時間常數 20 ms：轉旋鈕時跟手，又能把雜訊抹掉。
static float knob_coef = 0.f;

// tone：兩級一階低通（跟 bench/tone 同一套）
static float lp[2] = {0.f, 0.f};
static float sr    = 48000.f;

// LED 閃爍（block 計數，每秒 1000 個 block）
static uint32_t led_count = 0;
static bool     led_state = false;
static float    block_rate = 1000.f; // 每秒幾個 block，main 裡算

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // ── 每個 block：讀旋鈕（ADC 在背景用 DMA 一直轉換，這裡只是讀記憶體，不會卡）──
    for(int i = 0; i < kNumKnobs; i++)
    {
        const float raw = hw.adc.GetFloat(i);
        knob[i]         = knob[i] + knob_coef * (raw - knob[i]);
    }

    // 旋鈕 1 → tone 的截止頻率（倍數掃描，200 ~ 5000 Hz）
    const float fc = 200.f * expf(3.2188758f * knob[0]); // = 200 · 25^k，3.2188758 = ln 25（用 expf 比 powf 省 FLASH）
    const float a  = 1.f - expf(-2.f * 3.14159265f * fc / sr);

    // 旋鈕 2 → 音量
    const float vol = knob[1];

    // 旋鈕 3 → LED 閃爍週期：1000 ms（轉到 0）~ 50 ms（轉到 1）
    const uint32_t half_period = (uint32_t)((0.5f - 0.475f * knob[2]) * block_rate); // 秒 × 每秒 block 數
    if(++led_count >= half_period)
    {
        led_count = 0;
        led_state = !led_state;
        hw.SetLed(led_state);
    }

    // ── 每個 sample ──
    for(size_t i = 0; i < size; i++)
    {
        float x = demo.Process();
        lp[0] += a * (x - lp[0]);
        lp[1] += a * (lp[0] - lp[1]);
        const float y = lp[1] * vol;

        out[0][i] = y;
        out[1][i] = y;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);
    sr = hw.AudioSampleRate();

    // ADC：3 個通道，通道編號 = 陣列 index（adc.h:59,117,121,145）
    AdcChannelConfig cfg[kNumKnobs];
    cfg[0].InitSingle(seed::A0); // pin 22
    cfg[1].InitSingle(seed::A1); // pin 23
    cfg[2].InitSingle(seed::A2); // pin 24
    hw.adc.Init(cfg, kNumKnobs);
    hw.adc.Start(); // 內含校準，要在 StartAudio 之前（adc.cpp:426-428）
    System::Delay(2); // Start 不會等第一輪轉換；等 2 ms 再讀，初值才不是 0（審查發現）

    // 開機先把旋鈕值設成目前位置，免得一開機從 0 慢慢爬上來
    for(int i = 0; i < kNumKnobs; i++)
        knob[i] = hw.adc.GetFloat(i);

    demo.Init(sr);
    demo.SetToneHz(8000.f);

    block_rate = sr / (float)hw.AudioBlockSize(); // 每秒幾個 block
    knob_coef = 1.f - expf(-1.f / (0.020f * block_rate));

    hw.StartAudio(AudioCallback);

    // StartLog 放在 StartAudio 之後（同 in_probe）
    hw.StartLog(false);
    System::Delay(500);

    for(;;)
    {
        // 印整數（0 ~ 1000），不用 %f：libDaisy 預設的 printf 不支援浮點格式
        hw.PrintLine("k1=%d k2=%d k3=%d",
                     (int)(knob[0] * 1000.f),
                     (int)(knob[1] * 1000.f),
                     (int)(knob[2] * 1000.f));
        System::Delay(200);
    }
}
