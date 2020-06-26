
#include <concore/conc_reduce.hpp>
#if CONCORE_USE_TBB
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#endif

#if CONCORE_USE_OPENMP
#include <omp.h>
#endif

#include <benchmark/benchmark.h>
#include <numeric>

//! Produces a integer in range [-100, 100]
int rand_small_int() { return rand() % (200) - 100; }

char rand_char() {
    static constexpr int num = 'z'-'a';
    return static_cast<char>('a' + rand() % num);
}

std::string rand_string()
{
    int size = rand() % 100;
    std::string res(size, 'a');
    std::generate(res.begin(), res.end(), &rand_char);
    return res;
}

std::vector<int> generate_test_data(int size) {
    srand(0); // Same values each time
    std::vector<int> res(size, 0);
    std::generate(res.begin(), res.end(), &rand_small_int);
    return res;
}

std::vector<std::string> generate_string_test_data(int size) {
    srand(0); // Same values each time
    std::vector<std::string> res(size, std::string{});
    std::generate(res.begin(), res.end(), &rand_string);
    return res;
}

static void BM_std_accumulate(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<int> data = generate_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        auto res = std::accumulate(data.begin(), data.end(), int64_t(0));
        benchmark::DoNotOptimize(res);
    }
}

static void BM_conc_reduce(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<int> data = generate_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        auto res = concore::conc_reduce(
                data.begin(), data.end(), int64_t(0), std::plus<int64_t>(), std::plus<int64_t>());
        benchmark::DoNotOptimize(res);
    }
}

#if CONCORE_USE_TBB
static void BM_tbb_parallel_reduce(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<int> data = generate_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        struct sum_body {
            int64_t value{0};
            sum_body() = default;
            sum_body(sum_body& s, tbb::split) { value = 0; }
            void operator()(const tbb::blocked_range<int*>& r) {
                int64_t temp = value;
                for (int* a = r.begin(); a != r.end(); ++a) {
                    temp += *a;
                }
                value = temp;
            }
            void join(sum_body& rhs) { value += rhs.value; }
        };
        sum_body body;
        tbb::parallel_reduce(tbb::blocked_range(&data[0], &data[0] + data_size), body);
        benchmark::DoNotOptimize(body.value);
    }
}
#endif

#if CONCORE_USE_OPENMP
static void BM_omp_reduce(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<int> data = generate_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        int64_t sum = 0;
#pragma omp parallel for reduction(+ : sum)
        for (int idx = 0; idx < data_size; idx++) {
            sum += data[idx];
        };
        benchmark::DoNotOptimize(sum);
    }
}
#endif


static void BM_string_std_accumulate(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<std::string> data = generate_string_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        auto res = std::accumulate(data.begin(), data.end(), std::string{});
        benchmark::DoNotOptimize(res);
    }
}

static void BM_string_conc_reduce(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<std::string> data = generate_string_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        auto res = concore::conc_reduce(
                data.begin(), data.end(), std::string{}, std::plus<std::string>(), std::plus<std::string>());
        benchmark::DoNotOptimize(res);
    }
}

#if CONCORE_USE_TBB
static void BM_string_tbb_parallel_reduce(benchmark::State& state) {
    const int data_size = state.range(0);
    std::vector<std::string> data = generate_string_test_data(data_size);

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        CONCORE_PROFILING_SCOPE_N("test iter");
        struct sum_body {
            std::string value{0};
            sum_body() = default;
            sum_body(sum_body& s, tbb::split) { value.clear(); }
            void operator()(const tbb::blocked_range<std::string*>& r) {
                std::string temp = value;
                for (std::string* a = r.begin(); a != r.end(); ++a) {
                    temp += *a;
                }
                value = temp;
            }
            void join(sum_body& rhs) { value += rhs.value; }
        };
        sum_body body;
        tbb::parallel_reduce(tbb::blocked_range(&data[0], &data[0] + data_size), body);
        benchmark::DoNotOptimize(body.value);
    }
}
#endif

// #if CONCORE_USE_OPENMP
// static void BM_string_omp_reduce(benchmark::State& state) {
//     const int data_size = state.range(0);
//     std::vector<std::string> data = generate_string_test_data(data_size);

//     // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
//     for (auto _ : state) {
//         CONCORE_PROFILING_SCOPE_N("test iter");
//         std::string sum;
// #pragma omp parallel for reduction(+ : sum)
//         for (int idx = 0; idx < data_size; idx++) {
//             sum += data[idx];
//         };
//         benchmark::DoNotOptimize(sum);
//     }
// }
// #endif

static void BM_____(benchmark::State& /*state*/) {}
#define BENCHMARK_PAUSE() BENCHMARK(BM_____)

#define BENCHMARK_CASE(fun) BENCHMARK(fun)->Unit(benchmark::kMillisecond)->Arg(10'000'000);
#define BENCHMARK_CASE2(fun) BENCHMARK(fun)->Unit(benchmark::kMillisecond)->Arg(50'000);

BENCHMARK_CASE(BM_std_accumulate);
BENCHMARK_CASE(BM_conc_reduce);
#if CONCORE_USE_TBB
BENCHMARK_CASE(BM_tbb_parallel_reduce);
#endif
#if CONCORE_USE_OPENMP
BENCHMARK_CASE(BM_omp_reduce);
#endif
BENCHMARK_PAUSE();

BENCHMARK_CASE2(BM_string_std_accumulate);
BENCHMARK_CASE2(BM_string_conc_reduce);
#if CONCORE_USE_TBB
BENCHMARK_CASE2(BM_string_tbb_parallel_reduce);
#endif
// #if CONCORE_USE_OPENMP
// BENCHMARK_CASE2(BM_string_omp_reduce);
// #endif

BENCHMARK_MAIN();
