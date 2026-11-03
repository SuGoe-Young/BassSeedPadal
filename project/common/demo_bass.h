// demo_bass.h - 板子自己彈的假貝斯，用來取代 in[0]（2026-10-03）
//
// 為什麼要有這個：
//   每次測效果都要接貝斯、站在旁邊彈很累。而且真人彈每次力道都不一樣，
//   拿來「對照有效果 / 沒效果」反而不公平。這個訊號源每一輪都一模一樣，
//   你聽到的差異只會來自效果本身。
//
// 兩種樂句（Init 時選）：
//   kDynamics - 8 顆音一直彈，力道「大小小大小中小大」很不平均。給壓縮器這類看音量的效果。
//   kWithRests - 彈一顆、停一拍，中間有休止。給 noise gate：停下來的地方才聽得到底噪。
//   每格 0.3 秒，8 格一句（2.4 秒），反覆播放。
//
// 可選的底噪（SetNoise）：模仿真貝斯接線會有的嘶聲（白噪音）和 60 Hz 哼聲（台灣市電）。
//   預設關閉，所以壓縮器那支不受影響。
//
// 聲音怎麼做出來的（三個積木，即時路徑裡只做加減乘）：
//   1. 鋸齒波振盪器（DaisySP Oscillator，POLYBLEP 版本避免 aliasing）
//      → 鋸齒波泛音很多，像弦剛撥下去那種亮。
//   2. 一階低通濾波 → 把太亮的高頻削掉，聽起來才像貝斯，不像合成器的 buzz。
//   3. 包絡：換音先制音 2 ms，再很快升上去（約 5 ms），然後慢慢指數衰減，模仿撥弦後漸弱。
//
// 用法：
//   DemoBass demo;
//   demo.Init(hw.AudioSampleRate());   // 在 main() 裡，StartAudio 之前
//   float x = demo.Process();          // 在 AudioCallback 裡，每個 sample 一次

#pragma once

#include <math.h>
#include <stdint.h>
#include "daisysp.h"

class DemoBass
{
  public:
    // 峰值上限 0.30（約 -10 dBFS），跟 in_probe 量到的真貝斯峰值 0.455 同一個量級。
    // 刻意不給滿：壓縮器的 makeup 會把訊號再放大，留空間才不會削波。
    // 【2026-10-03】原本 0.40，審查算出壓縮器 makeup 後只剩不到 1 dB 就削波，降到 0.30。
    static constexpr float kLoud = 0.30f;
    static constexpr float kMid  = 0.18f;
    static constexpr float kSoft = 0.08f; // 約 -22 dBFS，故意放在常用門檻 -20 dBFS 底下
    static constexpr float kRest = 0.f;   // 休止：這格不撥，而且把上一顆悶掉

    static constexpr float kNoteSec   = 0.30f;  // 每格多長
    static constexpr int   kNumNotes  = 8;
    static constexpr float kDecaySec  = 0.35f;  // 衰減時間常數：0.35 秒掉到約 37%
    static constexpr float kAttackSec = 0.001f; // 起音「時間常數」1 ms；實際升到 99% 約 4.6 ms
    static constexpr float kDampSec   = 0.002f; // 換音前的制音長度
    static constexpr float kToneHz    = 800.f;  // 低通截止頻率，越低越悶
    static constexpr float kHumHz     = 60.f;   // 台灣市電頻率

    enum Pattern
    {
        kDynamics,
        kWithRests,
    };

    void Init(float sample_rate, Pattern pattern = kDynamics)
    {
        sr_ = sample_rate;
        osc_.Init(sample_rate);
        osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
        osc_.SetAmp(1.f);

        hum_.Init(sample_rate);
        hum_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        hum_.SetFreq(kHumHz);
        hum_.SetAmp(0.f);

        riff_ = (pattern == kWithRests) ? riff_rests_ : riff_dyn_;

        // 下面三個係數都用到 expf，所以在 Init 算好，callback 裡只用乘法。
        // 一階低通：y += a * (x - y)，a 越大反應越快、越亮。
        lp_coef_ = 1.f - expf(-2.f * 3.14159265f * tone_hz_ / sample_rate);
        // 衰減：每個 sample 乘一次 decay_，乘久了就是指數下降。
        decay_ = expf(-1.f / (kDecaySec * sample_rate));
        // 起音：每個 sample 往目標走一小步，跟低通是同一個公式。
        atk_coef_ = 1.f - expf(-1.f / (kAttackSec * sample_rate));

        samples_per_note_ = (uint32_t)lroundf(kNoteSec * sample_rate);
        damp_samples_     = (uint32_t)lroundf(kDampSec * sample_rate);
        counter_          = 0;
        idx_              = 0;
        env_              = 0.f;
        lp_               = 0.f;
        StartNote();
    }

    // 底噪大小（線性，0 = 關）。在 main() 裡設，不要在 callback 裡改。
    // 例：0.01 約 -40 dBFS，戴耳機在休止的地方聽得很清楚。
    void SetNoise(float hiss, float hum)
    {
        hiss_ = hiss;
        hum_.SetAmp(hum);
    }

    // 改假貝斯本身的亮度（內建低通的截止頻率）。在 main() 裡設，不要在 callback 裡改（用到 expf）。
    // 預設 800 Hz 聽起來像貝斯；測 tone / EQ 這類濾波效果時調高（例如 8000），
    // 原音要夠亮，濾波器才有東西可以削。
    void SetToneHz(float hz)
    {
        tone_hz_ = hz; // 記下來：就算在 Init 之前呼叫，Init 也會用這個值，不會被蓋回 800
        lp_coef_ = 1.f - expf(-2.f * 3.14159265f * hz / sr_);
    }

    // 一個樂句有幾個 sample。給效果程式對齊 A/B 切換點用。
    uint32_t SamplesPerPhrase() const { return samples_per_note_ * kNumNotes; }

    float Process()
    {
        if(++counter_ >= samples_per_note_)
        {
            counter_ = 0;
            idx_     = (idx_ + 1) % kNumNotes;
            StartNote();
        }

        // 換音分兩段，模仿真的撥弦：
        //   1. 制音（2 ms）：先把上一顆的餘音快速壓下去，像手指碰到弦。
        //   2. 起音：再往這顆的力道升上去。
        // 兩段都是「每個 sample 往目標走一小步」，音量是滑過去的，不會跳一格（跳一格 =「喀」）。
        // 休止格的制音會持續整格，等於手一直按著弦。
        //
        // 【2026-10-03 修正，rt-audio-review 抓到】原本沒有制音，結束條件只有
        // env_ >= 0.99 * target_。新音比上一顆的餘音還小聲時，第一個 sample 就成立，
        // 等於沒撥：同音高的 E2 整顆消失，8 顆音只聽得到 6 次撥弦。
        if(damp_left_ > 0)
        {
            env_ += atk_coef_ * (0.f - env_);
            --damp_left_;
        }
        else if(attacking_)
        {
            env_ += atk_coef_ * (target_ - env_);
            if(fabsf(env_ - target_) <= 0.01f * target_)
                attacking_ = false;
        }
        else
        {
            env_ *= decay_;
        }
        // 小到聽不見就直接歸零，免得一路乘下去變成 denormal（極小浮點數，有些 CPU 算很慢）。
        if(env_ < 1e-6f)
            env_ = 0.f;

        const float raw = osc_.Process();
        lp_ += lp_coef_ * (raw - lp_);

        float y = lp_ * env_;
        if(hiss_ > 0.f)
            y += hiss_ * WhiteNoise();
        y += hum_.Process(); // amp 為 0 時輸出就是 0
        return y;
    }

  private:
    struct Note
    {
        float freq; // Hz
        float vel;  // 這顆音的峰值（力道）；kRest = 休止
    };

    // E 小調的簡單 riff。音高在貝斯 E 弦、A 弦的低把位。
    // 用一般成員陣列，不用 static constexpr：專案是 C++14，static constexpr 陣列
    // 被變數索引時需要在 class 外再定義一次，否則連結失敗。

    // 力道排列：大、小、小、大、小、中、小、大 —— 重拍用力，其他輕帶。
    const Note riff_dyn_[kNumNotes] = {
        {82.41f, kLoud}, // E2
        {82.41f, kSoft}, // E2
        {98.00f, kSoft}, // G2
        {82.41f, kLoud}, // E2
        {110.0f, kSoft}, // A2
        {98.00f, kMid},  // G2
        {73.42f, kSoft}, // D2
        {82.41f, kLoud}, // E2
    };

    // 彈一顆停一拍。最後兩格連續休止，留一段比較長的安靜，底噪最明顯。
    // 有一顆小聲的 A2：用來聽 gate 門檻設太高時，小聲的音和尾音會不會被吃掉。
    const Note riff_rests_[kNumNotes] = {
        {82.41f, kLoud}, // E2
        {82.41f, kRest},
        {98.00f, kMid},  // G2
        {98.00f, kRest},
        {110.0f, kSoft}, // A2
        {110.0f, kRest},
        {82.41f, kRest},
        {82.41f, kRest},
    };

    const Note* riff_ = riff_dyn_;

    void StartNote()
    {
        if(riff_[idx_].vel <= 0.f)
        {
            // 休止：整格都在制音，不起音。音高不變，免得悶掉的餘音突然換調。
            target_    = 0.f;
            attacking_ = false;
            damp_left_ = samples_per_note_;
            return;
        }
        osc_.SetFreq(riff_[idx_].freq);
        target_    = riff_[idx_].vel;
        attacking_ = true;
        damp_left_ = damp_samples_;
    }

    // 白噪音：最簡單的線性同餘亂數（LCG），每個 sample 一個整數乘加。
    // 回傳 -1 ~ 1。音訊用途不需要品質很好的亂數，只要聽起來像嘶聲。
    float WhiteNoise()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return (float)(int32_t)rng_ * (1.f / 2147483648.f);
    }

    daisysp::Oscillator osc_;
    daisysp::Oscillator hum_;
    float               lp_coef_   = 0.f;
    float               decay_     = 0.f;
    float               atk_coef_  = 0.f;
    float               lp_        = 0.f;
    float               env_       = 0.f;
    float               target_    = 0.f;
    float               hiss_      = 0.f;
    float               sr_        = 48000.f;
    float               tone_hz_   = kToneHz;
    bool                attacking_ = false;
    uint32_t            rng_              = 22222u;
    uint32_t            damp_samples_     = 0;
    uint32_t            damp_left_        = 0;
    uint32_t            samples_per_note_ = 0;
    uint32_t            counter_          = 0;
    int                 idx_              = 0;
};
