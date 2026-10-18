#include "Core/BenchmarkStats.h"
#include <iostream>
int main()
{
    const auto stats = SummarizeTimings({4, 1, 3, 2});
    if (stats.mean != 2.5 || stats.median != 2.5 || stats.p95 != 4)
        return 1;
    std::vector<double> ordered;
    for (int i = 1; i <= 100; ++i)
        ordered.push_back(double(i));
    const auto hundred = SummarizeTimings(ordered);
    if (hundred.median != 50.5 || hundred.p95 != 95)
        return 2;
    if (SummarizeTimings({7}).median != 7)
        return 3;
    for (auto invalid : {std::vector<double>{}, std::vector<double>{-1}, std::vector<double>{NAN}})
    {
        try
        {
            SummarizeTimings(invalid);
            return 4;
        }
        catch (const std::runtime_error &)
        {
        }
    }
    std::cout << "Benchmark statistics tests passed\n";
}
