// -*- coding: utf-8 -*-
//
// 搜索算法的单元测试。
//
// 不用第三方框架：这一层是纯数学，一个进程内的断言足够，也免得给构建加依赖。
// 断言失败只记账不中断，跑完把结果一次性打出来（"all search tests passed"
// 是 CMake 的 add_test 认的标记行）。
//
// 覆盖的重点是"与 Python 版行为一致"的那些细节：区间开闭、瞄准区间中心、
// log-log 幂律外推、斜率夹取、第一次标定 72 DPI、以及目标不可达时的判定。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "common.hpp"
#include "i18n.hpp"
#include "search.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

bool close_to(double actual, double want, double tolerance = 1e-9) {
    const double scale = std::max(1.0, std::max(std::fabs(actual), std::fabs(want)));
    return std::fabs(actual - want) <= tolerance * scale;
}

using pdfc::Decision;
using pdfc::Strategy;
using pdfc::Trial;

constexpr std::int64_t kMib = 1024 * 1024;

/// 造一条幂律数据：size = k * scale^2（渲染后体积大致就是这么长的）。
Trial make_trial(double scale, int quality, std::int64_t size) {
    Trial trial;
    trial.scale = scale;
    trial.quality = quality;
    trial.size = size;
    trial.path = "fake.pdf";
    return trial;
}

void test_accept_range_and_is_accepted() {
    const std::int64_t target = 10 * kMib;

    const auto closest = pdfc::accept_range(target, Strategy::Closest);
    expect(close_to(closest.first, target * 0.95), "closest lower bound is 0.95t");
    expect(close_to(closest.second, target * 1.05), "closest upper bound is 1.05t");

    const auto smaller = pdfc::accept_range(target, Strategy::Smaller);
    expect(close_to(smaller.first, target * 0.90), "smaller lower bound is 0.90t");
    expect(close_to(smaller.second, static_cast<double>(target)), "smaller upper bound is t");

    // closest：闭区间 [0.95t, 1.05t]
    expect(!pdfc::is_accepted(static_cast<std::int64_t>(target * 0.9), target,
                              Strategy::Closest),
           "closest rejects 0.90t");
    expect(pdfc::is_accepted(static_cast<std::int64_t>(target * 0.95), target,
                             Strategy::Closest),
           "closest accepts exactly 0.95t");
    expect(pdfc::is_accepted(target, target, Strategy::Closest),
           "closest accepts exactly t");
    expect(pdfc::is_accepted(static_cast<std::int64_t>(target * 1.05), target,
                             Strategy::Closest),
           "closest accepts exactly 1.05t");
    expect(!pdfc::is_accepted(static_cast<std::int64_t>(target * 1.06), target,
                              Strategy::Closest),
           "closest rejects 1.06t");

    // smaller：半开区间 [0.9t, t)，等于目标不算命中
    expect(!pdfc::is_accepted(static_cast<std::int64_t>(target * 0.9) - 1, target,
                              Strategy::Smaller),
           "smaller rejects just below 0.90t");
    expect(pdfc::is_accepted(static_cast<std::int64_t>(target * 0.9), target,
                             Strategy::Smaller),
           "smaller accepts exactly 0.90t");
    expect(!pdfc::is_accepted(target, target, Strategy::Smaller),
           "smaller rejects exactly t");
    expect(!pdfc::is_accepted(static_cast<std::int64_t>(target * 1.05), target,
                              Strategy::Smaller),
           "smaller rejects 1.05t");
}

void test_aim_size() {
    const std::int64_t target = 8 * kMib;
    expect(pdfc::aim_size(target, Strategy::Closest) == target,
           "closest aims straight at the target");
    // smaller 的区间是 [0.9t, t)，瞄准 t 会一直贴在区间边界上，必须瞄准中心
    const auto range = pdfc::accept_range(target, Strategy::Smaller);
    const auto want = static_cast<std::int64_t>((range.first + range.second) / 2.0);
    expect(pdfc::aim_size(target, Strategy::Smaller) == want,
           "smaller aims at the middle of its range");
}

void test_pick_best() {
    std::vector<Trial> trials = {
        make_trial(1.0, 85, 4 * kMib),
        make_trial(2.0, 85, 12 * kMib),
        make_trial(3.0, 85, 9 * kMib),
    };
    const std::int64_t target = 10 * kMib;

    const Trial* best = pdfc::pick_best(trials, target, Strategy::Closest);
    expect(best != nullptr && best->size == 9 * kMib,
           "closest picks the size nearest to the target");
    expect(pdfc::pick_best({}, target, Strategy::Closest) == nullptr,
           "pick_best returns nullptr for an empty list");

    // smaller 只在"小于目标"的里面挑，并且要挑最接近的
    best = pdfc::pick_best(trials, target, Strategy::Smaller);
    expect(best != nullptr && best->size == 9 * kMib,
           "smaller picks the largest size below the target");

    // 全都超过目标时，smaller 只能输出最小的那个
    std::vector<Trial> all_over = {make_trial(2.0, 85, 12 * kMib),
                                   make_trial(1.0, 85, 11 * kMib)};
    best = pdfc::pick_best(all_over, target, Strategy::Smaller);
    expect(best != nullptr && best->size == 11 * kMib,
           "smaller falls back to the smallest result when nothing is below the target");
}

void test_predict_scale_power_law() {
    // size = k * scale^2，两个点足以把斜率解出来
    const double k = 100000.0;
    std::vector<Trial> trials = {
        make_trial(1.0, 85, static_cast<std::int64_t>(k * 1.0)),
        make_trial(2.0, 85, static_cast<std::int64_t>(k * 4.0)),
    };
    const std::int64_t target = 250000;      // 理想 scale = sqrt(2.5) ≈ 1.5811
    const auto predicted = pdfc::predict_scale(trials, target, 0.1, 8.5);
    expect(predicted.has_value(), "predict_scale returns a value for two points");
    expect(predicted.has_value() && close_to(*predicted, std::sqrt(2.5), 1e-6),
           "predict_scale inverts the power law");

    // 单点只能按缺省斜率 2.0 外推
    std::vector<Trial> single = {make_trial(1.0, 85, 100000)};
    const auto one = pdfc::predict_scale(single, 400000, 0.1, 8.5);
    expect(one.has_value() && close_to(*one, 2.0, 1e-6),
           "predict_scale uses the default slope with a single point");

    // 数据为空 → nullopt
    expect(!pdfc::predict_scale({}, target, 0.1, 8.5).has_value(),
           "predict_scale returns nullopt without data");

    // 无损结果（scale = 0）不参与测算
    std::vector<Trial> lossless_only = {make_trial(0.0, 0, 5 * kMib)};
    expect(!pdfc::predict_scale(lossless_only, target, 0.1, 8.5).has_value(),
           "predict_scale ignores lossless trials");

    // 目标远大于数据 → 顶到上界；远小于 → 顶到下界
    const auto high = pdfc::predict_scale(trials, 10LL * 1000 * 1000 * 1000, 0.1, 8.5);
    expect(high.has_value() && close_to(*high, 8.5), "predict_scale clamps to smax");
    const auto low = pdfc::predict_scale(trials, 1, 0.1, 8.5);
    expect(low.has_value() && close_to(*low, 0.1), "predict_scale clamps to smin");
}

void test_predict_quality() {
    // 同一 scale 下质量越高体积越大，取两个点就能插值
    std::vector<std::pair<int, std::int64_t>> points = {{50, 100000}, {100, 400000}};
    const auto quality = pdfc::predict_quality(points, 200000, 50, 100);
    expect(quality.has_value(), "predict_quality interpolates");
    // size ∝ q^2 → q = 50 * sqrt(2) ≈ 70.7 → 71
    expect(quality.has_value() && *quality == 71, "predict_quality solves the power law");
    expect(!pdfc::predict_quality({{50, 100000}}, 200000, 50, 100).has_value(),
           "predict_quality needs at least two points");
    const auto clamped = pdfc::predict_quality(points, 100000000, 50, 100);
    expect(clamped.has_value() && *clamped == 100, "predict_quality clamps to qmax");
}

void test_plan_next_calibrates_then_jumps() {
    const std::int64_t target = 2 * kMib / 4;      // 512 KB
    const double smin = pdfc::dpi_to_scale(7.2);
    const double smax = pdfc::dpi_to_scale(600.0);

    // 第一次：不管目标是多少，都先在 72 DPI（scale = 1.0）标定一次
    Decision decision = pdfc::plan_next({}, target, Strategy::Closest, smin, smax);
    expect(!decision.stop, "the first plan does not stop");
    expect(close_to(decision.scale, 1.0), "the first plan calibrates at 72 DPI");
    expect(decision.quality == pdfc::kQualityDefault, "the first plan uses the default quality");

    // 第二次：用标定结果直接跳到目标附近（单点 + 缺省斜率 2.0 → sqrt(target/size)）
    std::vector<Trial> trials = {make_trial(1.0, pdfc::kQualityDefault, 100000)};
    decision = pdfc::plan_next(trials, target, Strategy::Closest, smin, smax);
    expect(!decision.stop, "the second plan does not stop");
    const double expected =
        std::sqrt(static_cast<double>(target) / 100000.0);
    expect(close_to(decision.scale, expected, 1e-6),
           "the second plan jumps close to the target");
    expect(decision.quality == pdfc::kQualityDefault,
           "the second plan keeps the default quality");
}

void test_plan_next_unreachable() {
    const std::int64_t target = 10 * kMib;
    const double smin = pdfc::dpi_to_scale(7.2);
    const double smax = pdfc::dpi_to_scale(600.0);

    // 顶到最大 scale、最高质量，文件仍然远远小于目标 → 直接判定不可达
    std::vector<Trial> trials = {
        make_trial(1.0, pdfc::kQualityDefault, 20000),
        make_trial(smax, pdfc::kQualityMax, 100000),
    };
    const Decision decision =
        pdfc::plan_next(trials, target, Strategy::Closest, smin, smax);
    expect(decision.stop, "an unreachable target stops the search");
    expect(!decision.reason.empty(), "a stopped search explains why");

    // 顶到最小 scale、最低质量，文件仍然大于目标 → 同样不可达
    std::vector<Trial> too_big = {
        make_trial(smax, pdfc::kQualityDefault, 200 * kMib),
        make_trial(smin, pdfc::kQualityMin, 50 * kMib),
    };
    const Decision stuck =
        pdfc::plan_next(too_big, target, Strategy::Closest, smin, smax);
    expect(stuck.stop, "a target below the achievable range stops the search");
    expect(!stuck.reason.empty(), "that stop also carries a reason");
}

void test_plan_next_quality_knob_at_bound() {
    // scale 顶到上界（且边界上还没有任何数据点）时，第一次探测直接上最极端的一档，
    // 这样"目标不可达"两次渲染就能定论，不用逐档试完整个质量阶梯。
    const double smax = pdfc::dpi_to_scale(600.0);
    const std::int64_t target = 400 * kMib;
    std::vector<Trial> trials = {make_trial(1.0, pdfc::kQualityDefault, 4 * kMib)};
    const Decision decision =
        pdfc::plan_next(trials, target, Strategy::Closest, pdfc::dpi_to_scale(7.2), smax);
    expect(!decision.stop, "the quality knob keeps searching");
    expect(close_to(decision.scale, smax), "the quality knob stays at the scale bound");
    expect(decision.quality == pdfc::kQualityMax,
           "the first boundary probe jumps straight to qmax");

    // 边界上已经有了数据点：先按插值猜一个质量，猜不出来才退回基准质量
    std::vector<Trial> at_bound = {
        make_trial(1.0, pdfc::kQualityDefault, 4 * kMib),
        make_trial(smax, pdfc::kQualityDefault, 40 * kMib),
        make_trial(smax, pdfc::kQualityMax, 60 * kMib),
    };
    const Decision interpolated =
        pdfc::plan_next(at_bound, 50 * kMib, Strategy::Closest, pdfc::dpi_to_scale(7.2), smax);
    expect(!interpolated.stop, "the quality ladder keeps searching");
    expect(close_to(interpolated.scale, smax), "the quality ladder stays at the scale bound");
    expect(interpolated.quality > pdfc::kQualityDefault &&
               interpolated.quality < pdfc::kQualityMax,
           "the quality ladder interpolates between the measured points");
}

}  // namespace

int main() {
    test_accept_range_and_is_accepted();
    test_aim_size();
    test_pick_best();
    test_predict_scale_power_law();
    test_predict_quality();
    test_plan_next_calibrates_then_jumps();
    test_plan_next_unreachable();
    test_plan_next_quality_knob_at_bound();

    if (g_failures != 0) {
        std::printf("%d of %d search checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("all search tests passed (%d checks)\n", g_checks);
    return 0;
}
