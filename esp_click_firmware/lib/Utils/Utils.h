#pragma once

namespace Utils
{
    inline float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
    {
        float ratio = (x - in_min) / (in_max - in_min);
        float newRange = out_max - out_min;
        return ratio * newRange + out_min;
    }
};