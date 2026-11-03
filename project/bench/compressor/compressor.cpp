// compressor - 貝斯壓縮器。第二個效果。
//
// 【2026-10-03 改版】輸入預設改成板子自己彈的假貝斯（project/common/demo_bass.h），
//   不用接貝斯，只要接輸出聽。想用真貝斯就把 kUseDemoInput 改成 false。
//
// 這支現在是一個「聽對照」的實驗：
//   每 4.8 秒（= 假貝斯的兩個樂句）自動切換一次。
//   LED 暗 = 原音，沒經過壓縮器。
//   LED 亮 = 經過壓縮器。
//   假貝斯每一輪都彈得一模一樣，所以兩段聽起來的差別，全部來自壓縮器。
//
// 訊號鏈（只在 kUseDemoInput = false 時用到輸入）：
//   Bass -> MS-60B+ -> pin16 (AUDIO IN L)；pin18/pin20 -> EVO 4
//   pin17 沒接，只讀 in[0]，扇出到左右兩個輸出。
//
// 原理（見 DaisySP-LGPL/Source/Dynamics/compressor.cpp:39-50，核心 5 行）：
//   ① 取絕對值 → ② 快攻慢放的包絡 → ③ 換成 dB、減門檻、夾 0、乘 (1/ratio - 1)
//   → ④ 加 makeup、換回倍率 → ⑤ 乘上原訊號。
//   門檻以下 fmax(…, 0) 出來是 0，所以一根手指都不碰；
//   小聲變大是 makeup 抬整條線的結果，不是壓縮器本體做的。

#include "daisy_seed.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "../../common/demo_bass.h"
#include "../../common/ab_switch.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static Compressor comp;
static DemoBass   demo;

// --- 輸入來源 ---------------------------------------------------------------
// true  = 板子自己彈假貝斯（預設）。
// false = 讀 pin16 進來的真貝斯。
static const bool kUseDemoInput = true;

// A/B 對照（原音 / 壓縮後，每兩個樂句切換）：細節在 project/common/ab_switch.h
static AbSwitch ab;

// --- 要調的四個數字 ---------------------------------------------------------
// 門檻（dBFS）。真貝斯實測峰值約 -7 dBFS；假貝斯大聲的音約 -10、小聲的約 -22。
// 設 -20：大聲的音會被壓，小聲的音在門檻下面。但緊跟在大聲音後面的小聲音，
// 開頭還是會被壓一點點：上一顆的 release 還沒放完（這是拖尾，不是 bug）。
// 安全邊界：調低門檻或調高 ratio，makeup 會變大，大聲音的開頭可能削波。
static const float kThresholdDb = -20.0f;

// 比例。4:1 = 超過門檻的部分每 4 dB 只出 1 dB。貝斯常見 3~6。
static const float kRatio = 4.0f;

// 攻擊（秒）。10ms：讓每顆音開頭的瞬態先過去再壓，保留「啪」的顆粒感。
// 設太快（<2ms）瞬態會被壓平，聲音變鈍。
static const float kAttackSec = 0.010f;

// 釋放（秒）。100ms：音符衰減時增益要跟得上放回來，太慢會聽到「喘」。
static const float kReleaseSec = 0.100f;
// ---------------------------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    for(size_t i = 0; i < size; i++)
    {
        const float x = kUseDemoInput ? demo.Process() : in[0][i];

        // 壓縮器不管有沒有被聽到都一直跑：
        // 這樣它的包絡一直在追蹤音量，切進來的那一刻就是正確的增益，
        // 不會從零開始反應而在第一顆音冒出一個怪異的大聲。
        const float y_comp = comp.Process(x);

        const float y = ab.Process(x, y_comp);

        out[0][i] = y;
        out[1][i] = y;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    const float sr = hw.AudioSampleRate();

    demo.Init(sr);
    ab.Init(sr, 2 * demo.SamplesPerPhrase());

    // Init() 內部會先 SetRatio → SetAttack → SetRelease → SetThreshold，
    // 順序是為了避免除以零（compressor.cpp:27-33）。我們之後再覆蓋。
    comp.Init(sr);
    comp.SetThreshold(kThresholdDb);
    comp.SetRatio(kRatio);
    comp.SetAttack(kAttackSec);
    comp.SetRelease(kReleaseSec);

    // 自動算 makeup。注意 DaisySP 的公式是 |thresh - thresh/ratio| * 0.5
    // （compressor.h:201）——只補「拉回 0 dB」所需的一半：-20、4:1 時是 7.5 dB，
    // 不是 15。所以 0 dBFS 進 → -7.5 dBFS 出，unity 點在 -10 dBFS。
    // 這是刻意保守，給瞬態留餘裕。想全補要自己 SetMakeup(15.f)。
    // 底噪會一起抬 7.5 dB，嘶聲變明顯是正常代價，不是 bug。
    //
    // 必須放在 SetRatio() 之後：SetRatio 不重算 makeup（compressor.h:101-105），
    // 搬到前面會用 Init 的 ratio=2 算出錯的 5 dB，而且不會報錯。
    comp.AutoMakeup(true);

    hw.StartAudio(AudioCallback);

    // LED = 現在聽到的是不是壓縮後的聲音。
    // 同時也是這支韌體的識別簽名：4.8 秒亮、4.8 秒暗，其他 bench 都沒有這麼慢的節奏。
    for(;;)
    {
        hw.SetLed(ab.IsOn());
        System::Delay(1);
    }
}
