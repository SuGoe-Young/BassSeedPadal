// alias_demo - 用耳朵驗證 aliasing（2026-09-08）
//
// 為什麼訊號源是板子自己產生的，不是貝斯：
//   輸入端目前是死的（codec ADC 沒送資料，nz = 0/24048）。
//   但輸出鏈路 2026-09-06 19:00 才驗過是好的，所以把訊號源搬進板子裡，
//   這個練習就能在修好 ADC 之前先做完。
//
//   而且純正弦波其實是**更好的**測試訊號：hard clip 一個正弦波，
//   產生的只有奇次諧波，全部往上跑。所以任何往下跑的成分，
//   必定是超過 Nyquist 折返回來的——折返本來就是反向的。
//   拿真貝斯彈，弦本身的泛音、手指噪音、琴衍雜訊會把下行成分埋掉。
//
// 兩種模式八秒一趟交替，這個對照是整個練習的重點：
//   CLEAN - 不削波。從頭到尾只有一個音往上。
//   CLIP  - ×20 灌進 fclamp，再 ×0.2 拉回來。往上掃的同時有東西往下掉。
//
// 掃頻範圍 500Hz -> 6000Hz。
//
// 【2026-09-08 修正】原本是 80 -> 1200，實聽的結果是「只聽得到諧波，
//   聽不到折返」。原因寫在原本的註解裡就有，只是當時沒意識到那是設計缺陷：
//   f0 = 1200Hz 時最低的超標諧波是第 21 次（25.2kHz），折返到 22.8kHz——
//   一般成年人聽不到，而且振幅只有基頻的 1/21，被前面的大諧波蓋掉。
//   **上限剛好停在「alias 才剛開始發生」的那一點，等於設計成看不到現象。**
//
// 為什麼 6000Hz 會聽得到（hard clip 只生奇次諧波，振幅約以 1/n 衰減）：
//   f0 = 4000 時，7 次 =28k 折返到 20k、9 次 =36k 折返到 12k、11 次 =44k 折返到 4k。
//   f0 = 6000 時，5 次 =30k 折返到 18k、7 次 =42k 折返到 6k。
//   低次諧波（1/5、1/7、1/9）比 1/21 大一個數量級，而且折返後落在 4k~12k，
//   正好是耳朵最敏感的區域。
//
//   最關鍵的是**方向**：f0 從 3000 爬到 4000 時，第 9 次諧波從 27k 爬到 36k，
//   折返點反而從 21kHz 一路掉到 12kHz。主音往上、折返往下——
//   這個反向運動就是 alias 唯一無法偽裝的特徵，也是這支 demo 要你聽的東西。
//
//   下限改 500Hz（不是原本的 80）：指數掃頻的時間是按倍頻程分配的，
//   從 80 起跳會把大半時間花在什麼都不會發生的低頻段。
//
// 掃頻用「每個 block 乘一個固定係數」做指數上升，不是每次算 powf：
//   powf 放進 callback 是不必要的成本，而且指數掃頻本來就等價於等比遞增。
//   係數在 main() 裡用 expf/logf 算一次（那裡不是即時路徑，隨便算）。
//
// 音量：clip 之後訊號會一直貼在滿刻度，不衰減會非常吵。
//   輸出的 0.2f 是保護耳朵用的，不要拿掉。
//
// 接線：pin18 (AUDIO OUT L) -> Tip、pin20 (AGND) -> Sleeve -> EVO 4 Line In。
//       輸入端不需要接。不要接耳機。

#include "daisy_seed.h"
#include "daisysp.h"
#include <math.h>

using namespace daisy;
using namespace daisysp;

DaisySeed  hw;
Oscillator osc;

static const float kFreqLo   = 500.f;
static const float kFreqHi   = 6000.f;
static const float kSweepSec = 8.f;

// 灌大，逼諧波撞到 Nyquist。
static const float kDrive = 20.f;

// clip 之後訊號貼滿刻度，這個是護耳用的。
static const float kOutGain = 0.2f;

// 每個 block 的頻率遞增係數，main() 算好。
float g_step = 1.f;

// callback 寫、主迴圈讀，只是拿去印 log。
volatile float g_freq  = kFreqLo;
volatile bool  g_clip  = false;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // 掃頻狀態要跨 block 記住。
    static float freq = kFreqLo;
    static bool  clip = false;

    freq *= g_step;
    if(freq >= kFreqHi)
    {
        // 掃到頂就回到底，並切換模式——CLEAN / CLIP 交替出現，
        // 兩趟並排才聽得出差別。
        freq = kFreqLo;
        clip = !clip;
    }
    osc.SetFreq(freq);

    for(size_t i = 0; i < size; i++)
    {
        const float x = osc.Process();

        float y;
        if(clip)
        {
            y = x * kDrive;
            y = fclamp(y, -1.f, 1.f);   // hard clip：alias 就是這裡生出來的
            y *= kOutGain;
        }
        else
        {
            // 對照組不削波。乘同一個 kOutGain，兩趟音量才有可比性。
            y = x * kOutGain;
        }

        // pin17 沒接，左聲道扇出到兩個輸出。
        // （只寫 out[0] 的話 out[1] 會是上一輪的殘留內容。）
        out[0][i] = y;
        out[1][i] = y;
    }

    g_freq = freq;
    g_clip = clip;
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    hw.StartLog(false);

    const float samplerate = hw.AudioSampleRate();

    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(kFreqLo);
    osc.SetAmp(1.f);   // 削波的量由 kDrive 控制，振盪器本身給滿。

    // 指數掃頻：頻率每個 block 乘上固定係數。
    // blocks = 掃一趟要幾個 block；step = (hi/lo)^(1/blocks)。
    const float blocks = (samplerate / 48.f) * kSweepSec;
    g_step = expf(logf(kFreqHi / kFreqLo) / blocks);

    hw.StartAudio(AudioCallback);

    for(;;)
    {
        const float f    = g_freq;
        const bool  clip = g_clip;

        // PrintLine 內部是忙等（logger.h:111-113），只能放主迴圈。
        hw.PrintLine("alias_demo mode=%s f0=%dHz drive=%d out=0.2",
                     clip ? "CLIP " : "CLEAN",
                     (int)f,
                     (int)kDrive);

        // CLIP 那趟恆亮、CLEAN 那趟慢閃，不看序列埠也知道現在是哪一趟。
        hw.SetLed(clip ? true : ((System::GetNow() & 1023) < 512));

        System::Delay(250);
    }
}
