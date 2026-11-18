#pragma once

// Small in-place radix-2 complex FFT (sizes 2^n). Twiddles are precomputed once per size.

#include <cmath>
#include <vector>

namespace sasari
{

class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * i / n;
            cosT[(size_t) i] = (float) std::cos (a);
            sinT[(size_t) i] = (float) std::sin (a);
        }
        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return n; }

    /** Forward transform (inverse = conjugate trick, see inverse()). */
    void forward (float* re, float* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (i < j)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)];
                    const float wi = sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
            }
        }
    }

    /** Inverse transform including the 1/N scale. */
    void inverse (float* re, float* im) const
    {
        for (int i = 0; i < n; ++i)
            im[i] = -im[i];
        forward (re, im);
        const float s = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
        {
            re[i] *= s;
            im[i] = -im[i] * s;
        }
    }

private:
    int n = 0;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

} // namespace sasari
