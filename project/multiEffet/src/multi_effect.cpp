// multi_effect - 綜合效果器 第一版（2026-11-04）
//
// 訊號鏈（五個效果全部串在一起）：
//   輸入 → ① noise gate → ② 壓縮器 → ③ envelope filter → ④ tone → ⑤ delay → 輸出
//
// 三顆旋鈕（pin 22/23/24，接法見 bench/knob_test）：
//   旋鈕 1 → tone：悶（200 Hz）↔ 亮（12 kHz，幾乎全開）
//   旋鈕 2 → wah 的量：0 = 沒有 wah
//   旋鈕 3 → delay 的量：0 = 沒有 delay
//   noise gate、壓縮器用固定設定（「設好就不動」的效果）。
//
// 每個效果都是從 bench 裡已經驗證過、燒過的版本搬過來的：
//   gate → bench/noise_gate、壓縮器 → bench/compressor、
//   envelope filter → bench/env_filter、tone → bench/tone、delay → bench/delay
//
// 輸入：實體輸入 pin 16（貝斯 → MS-60B+ → 母座 Tip → pin 16，Sleeve → pin 20）。
//   ⚠ 音訊輸入絕對最大 ±1.8 V、滿刻度約 1 Vrms（datasheet p.2 Table 1、p.4 Table 3）
//     → MS-60B+ 音量從最小往上帶。
//   kUseDemoInput = true 可切回板子自己彈的假貝斯。
//
// LED：門開著（有在彈）就亮，停下來門關了就熄。
// 序列埠：每 0.2 秒印輸入/輸出峰值、門的狀態、三顆旋鈕（0~1000），
//   用 .claude/scripts/serial_monitor.ps1 看，或給 Claude 讀來調參數。

#include "daisy_seed.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "../../common/demo_bass.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static DemoBass demo;

// --- 輸入來源 ---------------------------------------------------------------
static const bool kUseDemoInput = false; // false = 讀 pin 16 的真貝斯

static float sr         = 48000.f;
static float block_rate = 1000.f; // 每秒幾個 block

// 時間常數 → 每個 sample（或每個 block）往目標走幾成
static float CoefFromSec(float sec, float rate)
{
    return 1.f - expf(-1.f / (sec * rate));
}

// =============================================================================
// ① noise gate（bench/noise_gate）
// =============================================================================
struct NoiseGate
{
    float threshold = 0.025f; // ≈ -32 dBFS
    float needle = 0.f, gain = 0.f;
    float needle_atk = 0.f, needle_rel = 0.f, open_coef = 0.f, close_coef = 0.f;
    uint32_t hold_samples = 0, hold_left = 0;

    void Init(float rate)
    {
        needle_atk   = CoefFromSec(0.001f, rate);
        needle_rel   = CoefFromSec(0.020f, rate);
        open_coef    = CoefFromSec(0.0005f, rate);
        close_coef   = CoefFromSec(0.080f, rate);
        hold_samples = (uint32_t)lroundf(0.050f * rate);
    }

    float Process(float x)
    {
        const float level = fabsf(x);
        needle += ((level > needle) ? needle_atk : needle_rel) * (level - needle);

        bool open;
        if(needle > threshold)
        {
            hold_left = hold_samples;
            open      = true;
        }
        else if(hold_left > 0)
        {
            --hold_left;
            open = true;
        }
        else
        {
            open = false;
        }

        gain += (open ? open_coef : close_coef) * ((open ? 1.f : 0.f) - gain);
        if(gain < 1e-6f)
            gain = 0.f;
        return x * gain;
    }
};

// =============================================================================
// ③ envelope filter（bench/env_filter）
// =============================================================================
struct EnvFilter
{
    Svf   svf;
    float env = 0.f, env_atk = 0.f, env_rel = 0.f;
    float band_gain = 1.f;
    static constexpr float kFcMin = 250.f, kFcMax = 2500.f;
    static constexpr float kRes   = 0.5f;
    // 靈敏度。審查模擬：真貝斯壓縮後延音段 env 約 0.47~0.58（撥弦波形），2.5 會讓大聲的音卡在頂端；
    // makeup 改 4.5 dB 後 env 再 ×0.71，取 1.5。最後以耳朵為準。
    static constexpr float kSens = 1.5f;

    void Init(float rate)
    {
        env_atk = CoefFromSec(0.002f, rate);
        env_rel = CoefFromSec(0.150f, rate);
        svf.Init(rate);
        svf.SetRes(kRes);
        svf.SetDrive(0.f); // 關掉飽和，否則越大聲壓越多（見 reports/2026-10-03-env-filter-rt-review.md）
        svf.SetFreq(kFcMin);
        band_gain = 2.f * (1.f - powf(kRes, 0.25f)); // 帶通在 f0 正規化成 1 倍
    }

    // 每個 block：用目前的 env 移動共振峰
    void UpdateControl()
    {
        const float k = fminf(kSens * env, 1.f);
        svf.SetFreq(kFcMin * powf(kFcMax / kFcMin, k));
    }

    // amount = 旋鈕 2（0 ~ 1）
    float Process(float x, float amount)
    {
        const float level = fabsf(x);
        env += ((level > env) ? env_atk : env_rel) * (level - env);
        svf.Process(x);
        const float wah = svf.Band() * band_gain;
        // amount 0：全乾聲。amount 1：一半乾聲 + 帶通（跟 bench/env_filter 一樣）
        return x * (1.f - 0.5f * amount) + wah * amount;
    }
};

// =============================================================================
// ④ tone（bench/tone）：兩級一階低通
// =============================================================================
struct ToneControl // 不叫 Tone：DaisySP 已經有一個 daisysp::Tone，會撞名
{
    float y[2] = {0.f, 0.f};
    float a    = 1.f;

    // 每個 block：旋鈕 1（0 ~ 1）→ 截止頻率 200 · 60^k = 200 Hz ~ 12 kHz → a
    void UpdateControl(float knob)
    {
        const float fc = 200.f * expf(4.0943446f * knob); // 4.0943446 = ln 60
        a              = 1.f - expf(-2.f * 3.14159265f * fc / sr);
    }

    float Process(float x)
    {
        y[0] += a * (x - y[0]);
        y[1] += a * (y[0] - y[1]);
        return y[1];
    }
};

// =============================================================================
// ⑤ delay（bench/delay）
// =============================================================================
static const size_t kDelayBufSamples = 36000; // 0.75 s 上限
static const size_t kDelaySamples    = 500 * 48; // 0.5 s
static_assert(kDelaySamples <= kDelayBufSamples - 2, "delay 時間超過 buffer");
static DelayLine<float, kDelayBufSamples> delay_line; // 144 KB，放內部 SRAM（512 KB）

// 回授 0.4：每次回聲剩 40%。wet 最大 0.4（bench/delay 是 0.45，這裡前面還有 wah 會加量，保守一點）
static const float kDelayFeedback = 0.4f;
static const float kDelayWetMax   = 0.4f;

// =============================================================================
// 全部的效果與旋鈕
// =============================================================================
static NoiseGate  gate;
static Compressor comp;
static EnvFilter  envf;
static ToneControl tone;

static const int     kNumKnobs = 3;
static volatile float knob[kNumKnobs] = {0.f, 0.f, 0.f};
static float          knob_coef       = 0.f;

// 給 main 迴圈印的診斷值
static volatile float in_peak = 0.f, out_peak = 0.f;
static volatile bool  door_open = false;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // ── 每個 block（1 ms）：讀旋鈕、更新控制值 ──
    for(int i = 0; i < kNumKnobs; i++)
        knob[i] = knob[i] + knob_coef * (hw.adc.GetFloat(i) - knob[i]);

    tone.UpdateControl(knob[0]);
    envf.UpdateControl();
    const float wah_amount = knob[1];
    const float delay_wet  = kDelayWetMax * knob[2];

    // ── 每個 sample：照順序過五個效果 ──
    for(size_t i = 0; i < size; i++)
    {
        const float x = kUseDemoInput ? demo.Process() : in[0][i];

        float y = gate.Process(x);         // ①
        y       = comp.Process(y);         // ②
        y       = envf.Process(y, wah_amount); // ③
        y       = tone.Process(y);         // ④

        const float echo = delay_line.Read(); // ⑤
        delay_line.Write(y + echo * kDelayFeedback);
        y = y + echo * delay_wet;

        out[0][i] = y;
        out[1][i] = y;

        const float ax = fabsf(x), ay = fabsf(y);
        if(ax > in_peak)
            in_peak = ax;
        if(ay > out_peak)
            out_peak = ay;
    }
    door_open = gate.gain > 0.5f;
    hw.SetLed(door_open);
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);
    sr         = hw.AudioSampleRate();
    block_rate = sr / (float)hw.AudioBlockSize();

    // 旋鈕：pin 22 / 23 / 24
    AdcChannelConfig cfg[kNumKnobs];
    cfg[0].InitSingle(seed::A0);
    cfg[1].InitSingle(seed::A1);
    cfg[2].InitSingle(seed::A2);
    hw.adc.Init(cfg, kNumKnobs);
    hw.adc.Start();
    System::Delay(2); // 等第一輪轉換完成，初值才不是 0
    for(int i = 0; i < kNumKnobs; i++)
        knob[i] = hw.adc.GetFloat(i);
    knob_coef = CoefFromSec(0.020f, block_rate);

    demo.Init(sr);

    gate.Init(sr);

    // 壓縮器：設定同 bench/compressor（-20 dBFS、4:1、10 ms / 100 ms）
    comp.Init(sr);
    comp.SetThreshold(-20.f);
    comp.SetRatio(4.f);
    comp.SetAttack(0.010f);
    comp.SetRelease(0.100f);
    // makeup 手動設 4.5 dB，不用 AutoMakeup（7.5 dB）。
    // 原因（2026-11-04 rt-audio-review 模擬）：gate 關門時壓縮器收到 0，增益停在 makeup 倍率；
    // 休止後第一個起音來不及被壓（attack 10 ms），真貝斯 0.455 × 2.37 = 1.08 會被硬削。
    // 4.5 dB（×1.68）時起音約 0.76，不削。
    comp.AutoMakeup(false);
    comp.SetMakeup(4.5f);

    envf.Init(sr);
    tone.UpdateControl(knob[0]);

    delay_line.Init();
    delay_line.SetDelay(kDelaySamples);

    hw.StartAudio(AudioCallback);

    hw.StartLog(false); // 在 StartAudio 之後；PC 沒在讀時下面的 PrintLine 會卡，但聲音都在 callback 不受影響
    System::Delay(500);

    for(;;)
    {
        const int i_pk = (int)(in_peak * 1000.f);
        const int o_pk = (int)(out_peak * 1000.f);
        in_peak  = 0.f;
        out_peak = 0.f;
        hw.PrintLine("in=%d out=%d gate=%d k1=%d k2=%d k3=%d",
                     i_pk,
                     o_pk,
                     door_open ? 1 : 0,
                     (int)(knob[0] * 1000.f),
                     (int)(knob[1] * 1000.f),
                     (int)(knob[2] * 1000.f));
        System::Delay(200);
    }
}
