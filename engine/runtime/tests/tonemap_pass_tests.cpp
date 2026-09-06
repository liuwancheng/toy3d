#include "renderscene/postprocess/tonemap_pass.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }
}

int main()
{
    using toy3d::tonemap_sdr_channel_reference;
    const float black = tonemap_sdr_channel_reference(0.0F, 0.0F);
    const float middle = tonemap_sdr_channel_reference(0.18F, 0.0F);
    const float white = tonemap_sdr_channel_reference(1.0F, 0.0F);
    const float highlight = tonemap_sdr_channel_reference(16.0F, 0.0F);
    check(black == 0.0F, "black must remain black");
    check(middle > black && white > middle && highlight > white,
        "the filmic curve must remain monotonic across representative HDR values");
    check(highlight <= 1.0F, "HDR highlights must compress into SDR range");
    check(tonemap_sdr_channel_reference(1.0F, 1.0F) > white,
        "one positive exposure stop must brighten a finite input");
    check(tonemap_sdr_channel_reference(-1.0F, 0.0F) == 0.0F,
        "negative radiance must be clamped before the curve");
    check(tonemap_sdr_channel_reference(
              std::numeric_limits<float>::infinity(), 0.0F) == 0.0F &&
          tonemap_sdr_channel_reference(
              1.0F, std::numeric_limits<float>::quiet_NaN()) == 0.0F,
        "non-finite reference inputs must not escape into framebuffer values");

    if (failure_count != 0)
    {
        return 1;
    }
    std::cout << "Tonemap pass tests passed\n";
    return 0;
}
