// noise_gate - 第三個效果（2026-10-03）
//
// 它解決什麼問題：
//   貝斯沒在彈的時候，線路還是有嘶聲和哼聲。彈的時候被音蓋住聽不到，
//   一停下來就露出來。noise gate 就是一道門：有在彈才開，沒在彈就關，把底噪關在外面。
//
// 它跟壓縮器是反過來的：
//   壓縮器：門檻「以上」出手，把大聲的轉小。
//   gate  ：門檻「以下」出手，把小聲的（底噪）轉到沒聲音。
//   就是 fmax 那張圖裡，門檻左邊那一段被拿來用。
//
// 它也有一支「音量表指針」，跟壓縮器一模一樣：
//   ① 耳朵（取絕對值）→ ② 指針（上衝快、下掉慢）→ ③ 指針跟門檻比，決定門要開還是關
//   → ④ 門不是瞬間開關，而是滑過去（開得快、關得慢）→ ⑤ 乘上原訊號。
//
// 輸入：預設是假貝斯「彈一顆停一拍」版本，加上 -40 dBFS 左右的嘶聲和 60 Hz 哼聲。
//   kUseDemoInput = false 就改讀 pin16 的真貝斯。
//
// 聽法（跟 compressor 一樣的 A/B）：每兩個樂句（4.8 秒）自動切換。
//   LED 一直暗 = 沒有 gate，休止的地方聽得到嘶嘶聲和嗡嗡聲。
//   LED 跟著音一閃一閃 = 有 gate。LED 亮 = 門開著，熄 = 門關了。
//   注意：停一拍（0.3 秒）的地方還是會先聽到約 0.1 秒的嘶聲才淡掉——
//   指針要 ~40 ms 才掉到門檻下，再加 hold 50 ms，門才開始關。
//   樂句最後連停兩拍的地方才會真正安靜。這是 hold / release 跟「休止多長」的取捨。

#include "daisy_seed.h"
#include "daisysp.h"
#include "../../common/demo_bass.h"
#include "../../common/ab_switch.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static DemoBass demo;

// --- 輸入來源 ---------------------------------------------------------------
static const bool kUseDemoInput = true;

// --- 要調的四個數字 ---------------------------------------------------------
// 門檻（線性，滿刻度 = 1）。0.025 ≈ -32 dBFS。
// 要比底噪高（假貝斯的底噪峰值最多 0.02，審查實測指針最高 0.016），又要比最小聲的音低（0.08）。
// 【2026-10-03 審查後】原本 0.03，降一點讓門早點開，小聲的音開頭比較不會被吃掉。
// 設太高：小聲的音和尾音會被一起關掉。設太低：底噪會把門撐開。
static const float kThreshold = 0.025f;

// 開門速度（秒）。要很快，否則每顆音開頭的「啪」會被門擋掉一截。
static const float kAttackSec = 0.0005f;

// 保持（秒）。指針掉到門檻下之後，門還要再撐這麼久才開始關。
// 沒有這段，指針在門檻附近上下晃時，門會一直開開關關，聽起來像「嗒嗒嗒」。
static const float kHoldSec = 0.050f;

// 關門速度（秒）。慢慢關，尾音才是淡出去，不是被一刀切掉。
static const float kReleaseSec = 0.080f;
// ---------------------------------------------------------------------------

// 音量表指針本身的速度（跟上面四個參數分開，通常不用動）。
// 上衝 1 ms；下掉 20 ms：要比貝斯一個波的長度（E1 約 24 ms）慢一點，指針才不會跟著波形抖到門檻下。
static const float kNeedleAttackSec  = 0.001f;
static const float kNeedleReleaseSec = 0.020f;

// 由 main() 換算成每個 sample 用的係數，callback 裡只做乘加。
static float    needle_atk = 0.f, needle_rel = 0.f;
static float    gate_open_coef = 0.f, gate_close_coef = 0.f;
static uint32_t hold_samples = 0;

// gate 的狀態
static float    needle    = 0.f; // 音量表指針
static float    gate_gain = 0.f; // 門開多大：0 = 全關，1 = 全開
static uint32_t hold_left = 0;

// A/B 對照（沒 gate / 有 gate，每兩個樂句切換）：細節在 project/common/ab_switch.h
static AbSwitch      ab;
static volatile bool door_open = false; // 給 LED 看：gate 那段而且門是開的

static float GateProcess(float x)
{
    // ① 耳朵
    const float level = fabsf(x);

    // ② 指針：比指針大用上衝速度，比指針小用下掉速度（跟壓縮器第 ②③ 行同一招）
    const float k = (level > needle) ? needle_atk : needle_rel;
    needle += k * (level - needle);

    // ③ 開還是關：指針在門檻上 → 開，並把保持時間補滿；
    //    指針在門檻下 → 先倒數保持時間，數完才真的關。
    bool open;
    if(needle > kThreshold)
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

    // ④ 門滑過去：開用開門速度，關用關門速度
    if(open)
        gate_gain += gate_open_coef * (1.f - gate_gain);
    else
        gate_gain += gate_close_coef * (0.f - gate_gain);
    if(gate_gain < 1e-6f)
        gate_gain = 0.f; // 免得一路乘成 denormal

    // ⑤ 乘上原訊號
    return x * gate_gain;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    for(size_t i = 0; i < size; i++)
    {
        const float x = kUseDemoInput ? demo.Process() : in[0][i];

        // gate 一直跑，切進來那一刻門的狀態就是對的
        const float y_gate = GateProcess(x);

        const float y = ab.Process(x, y_gate);

        door_open = ab.IsOn() && (gate_gain > 0.5f);

        out[0][i] = y;
        out[1][i] = y;
    }
}

// 時間常數 → 每個 sample 走幾成的係數。跟 DemoBass 裡的公式一樣。
static float CoefFromSec(float sec, float sr)
{
    return 1.f - expf(-1.f / (sec * sr));
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    const float sr = hw.AudioSampleRate();

    demo.Init(sr, DemoBass::kWithRests);
    demo.SetNoise(0.01f, 0.01f); // 嘶聲、哼聲各約 -40 dBFS

    needle_atk      = CoefFromSec(kNeedleAttackSec, sr);
    needle_rel      = CoefFromSec(kNeedleReleaseSec, sr);
    gate_open_coef  = CoefFromSec(kAttackSec, sr);
    gate_close_coef = CoefFromSec(kReleaseSec, sr);
    hold_samples    = (uint32_t)lroundf(kHoldSec * sr);

    ab.Init(sr, 2 * demo.SamplesPerPhrase());

    hw.StartAudio(AudioCallback);

    // LED = 門現在是不是開的（只在 gate 那 4.8 秒會亮）。
    //   gate 那段：彈的時候亮、休止時熄 → 你看得到門跟著音一開一關。
    //   沒 gate 那段：一直暗。
    // 這個節奏也是這支韌體的識別簽名，跟其他 bench 都不一樣。
    for(;;)
    {
        hw.SetLed(door_open);
        System::Delay(1);
    }
}
