// passthru - B 階段：驗證「輸入鏈路」，LED 當粗略電平表
//
// 訊號鏈：Bass -> MS-60B+（效果全關，當 buffer）-> 6.35mm TS 母座 -> Daisy
// 接線：母座 Tip -> pin16 (AUDIO IN L)、母座 Sleeve -> pin20 (AGND)
//       pin17 (AUDIO IN R) 沒接，所以只讀 in[0]，扇出到左右兩個輸出。
//
// 板載 LED 的四種狀態：
//   完全不亮    -> 韌體沒跑起來，燒錄沒成功
//   慢閃 ~1Hz   -> pin16 完全沒訊號（< -66dBFS）
//   快閃 ~8Hz   -> 有訊號但太小（-66 ~ -40dBFS）：接線是通的，MS-60B+ 要開大聲
//   恆亮        -> 電平足夠（> -40dBFS），輸入鏈路完全正常
//
// 「快閃」是這一版新增的關鍵狀態：它把「完全沒接到」和「接到了但太小聲」
// 分開，不用接序列埠就能看出差別。
//
// 板子：Daisy Seed Rev7 / 1.2（PCM3060）。
// 輸入是 line level，滿刻度 1Vrms = 0dBFS，絕對最大 ±1.8V。

#include "daisy_seed.h"
#include <math.h>

using namespace daisy;

DaisySeed hw;

// 電平足夠：0.01 ≈ -40dBFS ≈ 10mVrms
static const float kLoud = 0.01f;

// 勉強測得到：0.0005 ≈ -66dBFS ≈ 0.5mVrms
// 設這麼低是為了把「有東西」和「什麼都沒有」分開，
// 不是說這個電平能用。
static const float kFaint = 0.0005f;

// 每個 sample 的衰減係數。0.9999^9600 ≈ 0.38，訊號停掉後約 200ms
// 才掉到三分之一。這個時間必須比最低音的週期長——貝斯低音 E 是
// 41Hz、週期 24ms，200ms 有八倍餘裕，所以燈不會跟著波形抖。
static const float kRelease = 0.9999f;

// callback 寫、主迴圈讀。float 在 M7 上是 32-bit 對齊的單次存取，
// 這樣傳遞是安全的。
volatile float input_level = 0.f;

void MyCallback(AudioHandle::InputBuffer in,
                AudioHandle::OutputBuffer out,
                size_t size)
{
    // 包絡要跨 block 記住，所以是 static。
    static float env = 0.f;

    for (size_t i = 0; i < size; i++)
    {
        const float dry = in[0][i];

        // 音訊繞著 0 上下擺，取絕對值才是「離 0 多遠」。
        const float mag = fabsf(dry);

        // 快攻慢放：抓到峰值立刻跟上，之後慢慢放掉。
        env = (mag > env) ? mag : env * kRelease;

        // pin17 沒接，左聲道扇出到兩個輸出。
        out[0][i] = dry;
        out[1][i] = dry;
    }

    input_level = env;
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    hw.StartAudio(MyCallback);

    for (;;)
    {
        // volatile 只讀一次，後面都用這份快照比較，
        // 免得三個分支各讀到不同的值。
        const float    lvl = input_level;
        const uint32_t now = System::GetNow();

        bool led;
        if (lvl > kLoud)
            led = true;                     // 恆亮
        else if (lvl > kFaint)
            led = (now & 127) < 64;         // 快閃 ~8Hz
        else
            led = (now & 1023) < 512;       // 慢閃 ~1Hz

        hw.SetLed(led);
    }
}
