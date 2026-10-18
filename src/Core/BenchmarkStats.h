#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>
struct BenchmarkStats
{
    double mean, median, p95;
};
inline BenchmarkStats SummarizeTimings(std::vector<double> values)
{
    if (values.empty())
        throw std::runtime_error("No benchmark samples");
    for (double v : values)
        if (!std::isfinite(v) || v < 0)
            throw std::runtime_error("Invalid GPU time");
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    return {std::accumulate(values.begin(), values.end(), 0.0) / n,
            n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * 0.5,
            values[size_t(std::ceil(0.95 * double(n))) - 1]}; // nearest-rank p95
}
