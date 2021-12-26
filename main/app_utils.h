#pragma once

#include <MPU.hpp>

template<class T>
T axis_cmp(const mpud::axes_t<T> &a, const mpud::axes_t<T> &b)
{
    T max = abs(a.x - b.x);
    T v = abs(a.y - b.y);
    if (v > max) max = v;
    v = abs(a.z - b.z);
    if (v > max) max = v;
    return max;
}

