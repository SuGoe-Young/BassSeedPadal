// sine_test - 輸出鏈路的對照組（bench 版，2026-09-06；2026-09-08 加 LED 簽名）
//
// 為什麼在這個時間點又寫一支：
//   輸入側已診斷到「codec 的 ADC 沒在跑」（in_probe 量到 nz = 0/24048，
//   每個 sample 都是剛好 0）。要讓那個結論站得住，得先確定
//   **輸出側在同一塊板子、同一次接線下仍然是好的**——
//   否則「沒聲音」有兩個可能的來源，診斷就不收斂。
//
//   這支完全不碰 in[]，訊號自己產生。所以：
//     有嗶聲 -> DAC / 輸出接線 / EVO 4 全部正常，問題確定只在輸入側
//     沒嗶聲 -> 輸出側在 A 階段之後壞掉了，先修這裡，別再看 in_probe
//
// === 2026-09-08 修改：LED 改成「短─短─長」的識別簽名 ===
//
//   起因是踩到坑：原本 LED 是 1 秒亮 1 秒暗，而 in_probe 是 0.5 秒亮
//   0.5 秒暗（in_probe.cpp:174）。兩者都只是「規律地閃」，肉眼幾乎分不出，
//   結果「LED 在閃」被誤讀成「sine_test 在跑」，白查了一輪輸出接線。
//
//   現在 LED 走「短─短─長，然後靜一秒」，跟任何等間隔的閃法都不會搞混。
//   **嗶聲跟 LED 同步走同一個節奏**，所以聽得到的時候，耳朵也收得到同一個
//   識別碼——不必接序列埠就能確定板子上跑的是哪一支。
//
//   （原本 LED 與嗶聲同步的用意保留：沒聲音時仍能分辨
//    「韌體沒跑起來」（LED 不動）和「韌體在跑但聽不到」（LED 有簽名）。）
//
// 序列埠每個節奏週期印一行，內容含韌體識別字串——這是第二層證據。
//
// 接線（板子絲印的實體 pin 號）：
//   pin18 (AUDIO OUT L) -> Tip
//   pin20 (AGND)        -> Sleeve
//   -> EVO 4 Line In。不要接耳機（輸出是 line level，1Vrms / 100R）。

#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisySeed  hw;
Oscillator osc;

// 主迴圈寫、callback 讀。
volatile bool tone_on = true;

// sine_test 的識別簽名：短─短─長，然後靜一秒。
// 刻意用不等長的段落——in_probe 那種等間隔閃法一眼就能區別開。
struct PatternStep
{
    bool     on;
    uint32_t ms;
};

static const PatternStep kSignature[] = {
    {true, 150}, {false, 150},    // 短
    {true, 150}, {false, 150},    // 短
    {true, 600}, {false, 1000},   // 長 + 休止
};

static const size_t kSignatureLen = sizeof(kSignature) / sizeof(kSignature[0]);

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    for(size_t i = 0; i < size; i++)
    {
        // 靜音期間也要讓振盪器跑，相位才連續；
        // 直接停掉再啟動會在每次嗶聲的開頭產生喀噠聲。
        float sig = osc.Process();
        if(!tone_on)
            sig = 0.f;

        out[0][i] = sig;
        out[1][i] = sig;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);

    hw.StartLog(false);

    // 取樣率要在 Init() 之後才拿得到。
    const float samplerate = hw.AudioSampleRate();

    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(440.f);
    // 壓低音量：下游增益未知，先保護喇叭和耳朵，確認聽得到再往上調。
    osc.SetAmp(0.25f);

    hw.StartAudio(AudioCallback);

    uint32_t cycles = 0;

    for(;;)
    {
        // 走一輪簽名。tone_on 與 LED 同步，聲音和燈帶同一個識別碼。
        for(size_t i = 0; i < kSignatureLen; i++)
        {
            tone_on = kSignature[i].on;
            hw.SetLed(kSignature[i].on);
            System::Delay(kSignature[i].ms);
        }

        cycles++;

        // PrintLine 內部是忙等（logger.h:111-113），所以只能放主迴圈，
        // 絕不能進 callback。放在節奏跑完之後，不打斷段落的時間精度。
        hw.PrintLine("sine_test 440Hz amp=0.25 sr=%d sig=short-short-long cycles=%d",
                     (int)samplerate,
                     (int)cycles);
    }
}
