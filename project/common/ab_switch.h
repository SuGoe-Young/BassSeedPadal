// ab_switch.h - A/B 聽覺對照：原音和效果後的聲音自動輪流播（2026-10-03）
//
// 這個跟效果本身無關，只是實驗工具。做成真踏板時不會用到。
// 從 compressor / noise_gate 抽出來，讓效果的 .cpp 只剩效果本身。
//
// 做的事：
//   每 period 個 sample 切換一次「聽原音 / 聽效果」。
//   切換時不硬切，用 5 ms 交叉淡化：兩邊音量不一樣，瞬間換過去波形會斷一格，聽起來是「喀」。
//
// 用法：
//   AbSwitch ab;
//   ab.Init(sr, 2 * demo.SamplesPerPhrase());   // main() 裡，StartAudio 之前
//   out = ab.Process(x, effect(x));               // callback 裡，每個 sample 一次
//   hw.SetLed(ab.IsOn());                         // main 迴圈裡，LED 顯示現在是不是效果
//
// 效果要「一直跑」，不要只在 IsOn() 時才跑：
//   這樣它的內部狀態（指針、門）一直在追蹤聲音，切進來那一刻就是對的。

#pragma once

#include <math.h>
#include <stdint.h>

class AbSwitch
{
  public:
    void Init(float sample_rate, uint32_t period_samples)
    {
        period_  = period_samples;
        counter_ = 0;
        on_      = false; // 先聽原音
        mix_     = 0.f;
        coef_    = 1.f - expf(-1.f / (0.005f * sample_rate));
    }

    // dry = 原音，wet = 效果後。回傳現在該輸出的聲音。
    float Process(float dry, float wet)
    {
        if(++counter_ >= period_)
        {
            counter_ = 0;
            on_      = !on_;
        }
        const float target = on_ ? 1.f : 0.f;
        mix_ += coef_ * (target - mix_);
        if(mix_ < 1e-6f)
            mix_ = 0.f; // 免得停在 denormal（極小浮點數）
        return dry + mix_ * (wet - dry);
    }

    // callback 寫、main 迴圈讀，所以 on_ 是 volatile。
    bool IsOn() const { return on_; }

  private:
    uint32_t      period_  = 1;
    uint32_t      counter_ = 0;
    volatile bool on_      = false;
    float         mix_     = 0.f;
    float         coef_    = 0.f;
};
