// board_id - 查明這塊板子的版本辨識腳，以及 libDaisy 認成了什麼
//
// 為什麼需要這支：
//   Shawn 的板子是 Rev7 / Seed 1.2（datasheet p.13：Rev7 的版本腳是 PD5）。
//   但這版 libDaisy（2024-01-08）的 CheckBoardVersion() 只讀 PD3 和 PD4
//   （daisy_seed.cpp:356-378），BoardVersion enum 也只有三個值
//   （daisy_seed.h:156-170），沒有 Rev7。
//   若 Rev7 的辨識腳真的是 PD5，libDaisy 會一路 fall through，
//   把 PCM3060 的板子當成 AK4556 的 Rev4 來初始化。
//   症狀會是「輸出正常、輸入全死」——正好是我們遇到的。
//
//   這支不做任何音訊，只把兩件事用閃燈報出來，把推論換成觀測。
//
// 讀法（兩組閃爍，中間有長暫停）：
//
//   第一組 = 哪一支版本辨識腳被接地
//     1 下 -> PD3 接地（libDaisy 會判成 Seed 1.1 / Rev5）
//     2 下 -> PD4 接地（libDaisy 會判成 Seed 2 DFM）
//     3 下 -> PD5 接地（libDaisy 讀都不讀 -> 這就是問題所在）
//     4 下 -> 三支都沒接地
//
//   ── 長暫停 2 秒 ──
//
//   第二組 = libDaisy 的 CheckBoardVersion() 實際回傳什麼
//     1 下 -> DAISY_SEED       (Rev4, AK4556)
//     2 下 -> DAISY_SEED_1_1   (Rev5, WM8731)
//     3 下 -> DAISY_SEED_2_DFM (PCM3060)
//
//   ── 長暫停 3 秒，然後重複 ──
//
// 若看到「3 下 ... 1 下」，就證實了：板子是 PD5（Rev7），
// 但 libDaisy 把它當成 Rev4。

#include "daisy_seed.h"

using namespace daisy;

DaisySeed hw;

// 閃 n 下，每下亮 200ms 暗 200ms。
static void BlinkTimes(int n)
{
    for (int i = 0; i < n; i++)
    {
        hw.SetLed(true);
        System::Delay(200);
        hw.SetLed(false);
        System::Delay(200);
    }
}

int main(void)
{
    hw.Init();

    // 用跟 libDaisy CheckBoardVersion() 完全相同的手法讀腳位：
    // 輸入模式 + 內部提升電阻，被接地的那支會讀到 0。
    // （出處：daisy_seed.cpp:363-370）
    GPIO pd3, pd4, pd5;
    Pin  pin_pd3(PORTD, 3);
    Pin  pin_pd4(PORTD, 4);
    Pin  pin_pd5(PORTD, 5);

    pd3.Init(pin_pd3, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    pd4.Init(pin_pd4, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    pd5.Init(pin_pd5, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // 提升電阻穩定需要一點時間，讀之前先等一下。
    System::Delay(10);

    int grounded_pin;
    if (!pd3.Read())
        grounded_pin = 1;
    else if (!pd4.Read())
        grounded_pin = 2;
    else if (!pd5.Read())
        grounded_pin = 3;
    else
        grounded_pin = 4;

    int detected;
    switch (hw.CheckBoardVersion())
    {
        case DaisySeed::BoardVersion::DAISY_SEED:       detected = 1; break;
        case DaisySeed::BoardVersion::DAISY_SEED_1_1:   detected = 2; break;
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: detected = 3; break;
        default:                                        detected = 4; break;
    }

    for (;;)
    {
        BlinkTimes(grounded_pin);
        System::Delay(2000);

        BlinkTimes(detected);
        System::Delay(3000);
    }
}
