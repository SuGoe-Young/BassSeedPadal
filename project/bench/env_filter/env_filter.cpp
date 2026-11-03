// env_filter - 第五個效果：envelope filter（auto-wah）（2026-10-03）
//
// 架構（三塊）：
//   ① 包絡偵測  env[n] = env[n-1] + a·(|x[n]| - env[n-1])，a = attack 或 release
//   ② 映射      f0 = kFcMin · (kFcMax / kFcMin) ^ min(kSens·env, 1)
//   ③ 二階濾波器 DaisySP Svf，中心頻率 f0，取帶通輸出 Band()
//   注意：帶通沒有「截止頻率」，f0 是共振峰的中心；兩邊都削
//   輸出 = 乾聲 × kDry + 帶通 × kWet
//
// ①、② 每個 sample / 每個 block 的分工：
//   env 每個 sample 更新（只有乘加）。
//   f0 和 Svf::SetFreq 每個 block（1 ms）算一次：SetFreq 內部有 sinf（svf.cpp:62-65）。
//
// 輸入：假貝斯「力道不均」版，內建低通調到 8000 Hz。
//   wah 是靠共振峰去凸顯泛音，原音要有足夠的泛音才聽得出來。
//
//   A/B 每兩個樂句（4.8 秒）切換。LED 一直暗 = 原音；LED 跟著大聲的音閃 = envelope filter。
//   大聲的音：f0 一下衝到高頻，再隨音量衰減往下掃（「哇——嗚」）。
//   小聲的音：f0 只到低一點的地方。

#include "daisy_seed.h"
#include "daisysp.h"
#include "../../common/demo_bass.h"
#include "../../common/ab_switch.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

static DemoBass demo;
static Svf      svf;
static AbSwitch ab;

// --- 輸入來源 ---------------------------------------------------------------
static const bool kUseDemoInput = true;

// --- ① 包絡偵測 ------------------------------------------------------------
// 跟壓縮器一樣：上升快、下降慢。release 決定「哇」之後往下掃多慢。
static const float kEnvAttackSec  = 0.002f;
static const float kEnvReleaseSec = 0.150f;

// --- ② 映射 ----------------------------------------------------------------
// f0 掃描範圍。貝斯 wah 常用的區段：太低只剩嗡嗡聲，太高變刺。
static const float kFcMin = 250.f;
static const float kFcMax = 2500.f;
// 靈敏度：env × kSens = 1 時掃到 kFcMax。
// 假貝斯最大聲的音 env 最高約 0.25（審查模擬；attack 只有 2 ms，env 幾乎貼著波峰），
// ×3.2 ≈ 0.8：大聲的音推到接近頂端，但不會卡在 2500 Hz，聽得到完整的往下掃。
static const float kSens = 3.2f;

// --- ③ 二階濾波器 ------------------------------------------------------------
// res 0~1，越大 Q 越大、哇越尖。1 會自己振盪，不要碰。
// Svf 內部：damp = 2·(1 − res^0.25)（svf.cpp:76），Q ≈ 1/damp。
//   res 0.5 → damp ≈ 0.32 → Q ≈ 3.1
static const float kRes = 0.5f;

// --- 輸出混合 ----------------------------------------------------------------
// 帶通會把 f0 以外都削掉，包括貝斯最重要的低頻。所以混一部分乾聲回來保住低頻。
static const float kDry = 0.5f;
static const float kWet = 1.0f;

// 由 main() 算好
static float env_atk = 0.f, env_rel = 0.f;
static float band_gain = 1.f; // 把帶通在 f0 的增益正規化成 1（見 main）

// 狀態
static float env = 0.f;
static volatile bool led_on = false; // 給 LED 看

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // ② 每個 block：用目前的 env 算 f0，設給 Svf（控制速率）
    const float k  = fminf(kSens * env, 1.f);
    const float f0 = kFcMin * powf(kFcMax / kFcMin, k);
    svf.SetFreq(f0);
    led_on = ab.IsOn() && (k > 0.5f); // 效果開著、而且 f0 被推到一半以上

    for(size_t i = 0; i < size; i++)
    {
        const float x = kUseDemoInput ? demo.Process() : in[0][i];

        // ① 包絡：對 |x| 做一階低通，上升用 attack、下降用 release
        const float level = fabsf(x);
        const float a     = (level > env) ? env_atk : env_rel;
        env += a * (level - env);

        // ③ 二階濾波器：每個 sample Process 一次，再取帶通輸出
        svf.Process(x);
        const float wah = svf.Band() * band_gain;

        const float y = kDry * x + kWet * wah;

        const float o = ab.Process(x, y);
        out[0][i] = o;
        out[1][i] = o;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    const float sr = hw.AudioSampleRate();

    demo.Init(sr);
    demo.SetToneHz(8000.f);

    env_atk = 1.f - expf(-1.f / (kEnvAttackSec * sr));
    env_rel = 1.f - expf(-1.f / (kEnvReleaseSec * sr));

    svf.Init(sr);
    svf.SetRes(kRes);
    // drive 要關掉。Svf::Init 的預設 drive 會在共振迴路裡加一個 band³ 的飽和項（svf.cpp:38,49），
    // 越大聲的音被壓越多，把力道差異抹平——正好跟 auto-wah 要「跟著力道走」相反。
    // （2026-10-03 rt-audio-review 模擬抓到：預設 drive 下 wah 比設計值小 5~11 dB。）
    svf.SetDrive(0.f);
    svf.SetFreq(kFcMin);

    // 帶通輸出在 f0 的增益約等於 Q = 1/damp（drive = 0 時成立，審查模擬 0.993~1.000）。乘上 damp，讓 f0 處剛好 1 倍，
    // 共振只負責「凸顯 f0 附近、削掉其他」，不會把整體音量放大 Q 倍而削波。
    band_gain = 2.f * (1.f - powf(kRes, 0.25f));

    ab.Init(sr, 2 * demo.SamplesPerPhrase());

    hw.StartAudio(AudioCallback);

    // LED：原音那 4.8 秒一直暗；envelope filter 那 4.8 秒，大聲的音把 f0 推高時閃一下。
    // 看得到「哇」跟著力道走，也是這支韌體獨有的節奏。
    for(;;)
    {
        hw.SetLed(led_on);
        System::Delay(1);
    }
}
