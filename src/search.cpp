// -*- coding: utf-8 -*-
//
// 目标大小搜索的实现。
//
// 纯数学：没有全局状态，也不碰 PDF 文件，所以 tests/test_search.cpp 里
// 可以直接喂假数据验证。算法与 Python 版 pdfcomp/core.py 的"参数搜索"段
// 逐条对应，连取整语义都对齐（见 round_half_even 的注释），保证同一份 PDF
// 在两版里走同一条搜索路径、得到同一个结果大小。

#include "search.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "common.hpp"
#include "i18n.hpp"

namespace pdfc {
namespace {

double log_or_floor(double value) {
    // 与 Python 的 _log 一致：0 取不到负无穷，兜一个极小值
    return std::log(std::max(value, 1e-9));
}

/// Python 的 round() 是"四舍六入五成双"，std::llround 是"五入"。
/// 质量落在恰好 .5 的概率极低，但两版必须给出同一个质量，所以照抄语义。
double round_half_even(double value) {
    const double lower = std::floor(value);
    const double diff = value - lower;
    if (diff > 0.5) return lower + 1.0;
    if (diff < 0.5) return lower;
    return std::fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

/// Python 的 round(x, 4)。只用来给"试过的 scale"做去重键：后面还有 1% 的
/// 相对容差（same_scale），所以这里不需要 dtoa 级别的正确舍入。
double round_decimals(double value, int decimals) {
    const double factor = std::pow(10.0, decimals);
    return round_half_even(value * factor) / factor;
}

/// 两个 scale 是否算"同一个"（相对误差 1% 以内）。
bool same_scale(double a, double b, double rel = 0.01) {
    const double largest = std::max(std::max(std::fabs(a), std::fabs(b)), 1e-9);
    return std::fabs(a - b) <= rel * largest;
}

std::int64_t size_distance(std::int64_t left, std::int64_t right) {
    return left > right ? left - right : right - left;
}

/// (scale, size)，按 scale 升序去重——对应 Python 的 sorted({(t.scale, t.size) ...})。
using ScalePoint = std::pair<double, std::int64_t>;
/// (quality, size)，按 quality 升序去重。
using QualityPoint = std::pair<int, std::int64_t>;

std::vector<ScalePoint> scale_points(const std::vector<Trial>& trials) {
    std::set<ScalePoint> unique;
    for (const Trial& trial : trials) {
        if (trial.rendered() && trial.size > 0) unique.insert({trial.scale, trial.size});
    }
    return {unique.begin(), unique.end()};
}

/// 只认"scale 正好顶在边界上"的点：Python 用的是
/// abs(t.scale - bound) <= 1e-6 + bound * 1e-6
std::vector<QualityPoint> quality_points(const std::vector<const Trial*>& trials,
                                         double bound) {
    const double tolerance = 1e-6 + bound * 1e-6;
    std::set<QualityPoint> unique;
    for (const Trial* trial : trials) {
        if (std::fabs(trial->scale - bound) <= tolerance) {
            unique.insert({trial->quality, trial->size});
        }
    }
    return {unique.begin(), unique.end()};
}

/// scale 已顶到边界时改用 JPEG 质量作为副旋钮。
///
/// 第一次探测边界时直接用最极端的一档（要变小就用最低质量，要变大就用最高
/// 质量），这样"目标不可达"只需 2 次渲染就能确定，而不是逐档试完整个质量阶梯。
Decision bound_plan(double bound, const std::vector<const Trial*>& trials,
                    std::int64_t target, Strategy strategy, int qmin, int qmax,
                    int base_quality, bool grow) {
    const auto [low, high] = accept_range(target, strategy);
    const std::int64_t aim = aim_size(target, strategy);
    const std::vector<QualityPoint> points = quality_points(trials, bound);

    std::set<int> tried;
    for (const auto& point : points) tried.insert(point.first);
    const int extreme = grow ? qmax : qmin;

    if (points.empty()) {
        // 一个点都没有：先拿最极端的一档探边界
        return Decision{bound, extreme, false, {}};
    }

    // 极端档已经试过了，那一档就是"再无可调"，可以直接判定目标不可达
    const auto extreme_at =
        std::find_if(points.begin(), points.end(),
                     [extreme](const QualityPoint& point) { return point.first == extreme; });
    if (extreme_at != points.end()) {
        const std::int64_t extreme_size = extreme_at->second;
        if (grow && static_cast<double>(extreme_size) < low) {
            return Decision{
                0.0, base_quality, true,
                tr("已达最大设置（{dpi:.0f} DPI / JPEG {qmax}），文件只有 {size}，"
                   "无法升到目标 {target:.2f} MB 附近",
                   {{"dpi", fmt_fixed(scale_to_dpi(bound), 0)},
                    {"qmax", std::to_string(qmax)},
                    {"size", human_size(static_cast<double>(extreme_size))},
                    {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}})};
        }
        if (!grow && static_cast<double>(extreme_size) > high) {
            return Decision{
                0.0, base_quality, true,
                tr("已达最小设置（{dpi:.0f} DPI / JPEG {qmin}），文件仍有 {size}，"
                   "无法降到目标 {target:.2f} MB 附近",
                   {{"dpi", fmt_fixed(scale_to_dpi(bound), 0)},
                    {"qmin", std::to_string(qmin)},
                    {"size", human_size(static_cast<double>(extreme_size))},
                    {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}})};
        }
    }

    const std::optional<int> predicted = predict_quality(points, aim, qmin, qmax);
    if (predicted.has_value() && tried.find(*predicted) == tried.end()) {
        return Decision{bound, *predicted, false, {}};
    }

    // 还差一个基准点才能插值
    if (tried.find(base_quality) == tried.end()) {
        return Decision{bound, base_quality, false, {}};
    }

    // 质量阶梯从"最接近预测值"的那一档开始试：Python 的 sorted(key=...) 是稳定
    // 排序，并列时保持质量升序，这里用 stable_sort 复现同样的顺序。
    const int center = predicted.value_or(base_quality);
    std::vector<int> ladder;
    ladder.reserve(static_cast<std::size_t>(qmax - qmin + 1));
    for (int quality = qmin; quality <= qmax; ++quality) ladder.push_back(quality);
    std::stable_sort(ladder.begin(), ladder.end(), [center](int left, int right) {
        return std::abs(left - center) < std::abs(right - center);
    });
    for (int quality : ladder) {
        if (tried.find(quality) == tried.end()) {
            return Decision{bound, quality, false, {}};
        }
    }

    std::int64_t smallest = points.front().second;
    std::int64_t largest = points.front().second;
    for (const auto& point : points) {
        smallest = std::min(smallest, point.second);
        largest = std::max(largest, point.second);
    }
    return Decision{
        0.0, base_quality, true,
        tr("{dpi:.0f} DPI 下所有 JPEG 质量都已试过，文件大小只能落在 {low} ~ {high} 之间，"
           "无法逼近目标 {target:.2f} MB",
           {{"dpi", fmt_fixed(scale_to_dpi(bound), 0)},
            {"low", human_size(static_cast<double>(smallest))},
            {"high", human_size(static_cast<double>(largest))},
            {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}})};
}

}  // namespace

double Trial::dpi() const { return scale_to_dpi(scale); }

std::pair<double, double> accept_range(std::int64_t target, Strategy strategy) {
    const double value = static_cast<double>(target);
    if (strategy == Strategy::Smaller) {
        return {value * (1.0 - kAcceptSmaller), value};
    }
    return {value * (1.0 - kAcceptClosest), value * (1.0 + kAcceptClosest)};
}

bool is_accepted(std::int64_t size, std::int64_t target, Strategy strategy) {
    const auto [low, high] = accept_range(target, strategy);
    const double value = static_cast<double>(size);
    if (strategy == Strategy::Smaller) {
        // smaller 策略要求"必须小于目标"，所以上界是开区间
        return value >= low && value < high;
    }
    return value >= low && value <= high;
}

const Trial* pick_best(const std::vector<Trial>& trials, std::int64_t target,
                       Strategy strategy) {
    if (trials.empty()) return nullptr;

    if (strategy == Strategy::Smaller) {
        const Trial* best = nullptr;
        for (const Trial& trial : trials) {
            if (trial.size >= target) continue;
            // 优先最接近目标，其次分辨率高、质量高
            if (best == nullptr || size_distance(target, trial.size) <
                                       size_distance(target, best->size) ||
                (size_distance(target, trial.size) == size_distance(target, best->size) &&
                 (trial.scale > best->scale ||
                  (trial.scale == best->scale && trial.quality > best->quality)))) {
                best = &trial;
            }
        }
        if (best != nullptr) return best;
        // 全都大于目标：那就输出最小的那一个
        for (const Trial& trial : trials) {
            if (best == nullptr || trial.size < best->size) best = &trial;
        }
        return best;
    }

    const Trial* best = nullptr;
    for (const Trial& trial : trials) {
        if (best == nullptr ||
            size_distance(trial.size, target) < size_distance(best->size, target) ||
            (size_distance(trial.size, target) == size_distance(best->size, target) &&
             trial.scale > best->scale)) {
            best = &trial;
        }
    }
    return best;
}

std::optional<double> predict_scale(const std::vector<Trial>& trials,
                                    std::int64_t target, double smin, double smax) {
    const std::vector<ScalePoint> points = scale_points(trials);
    if (points.empty()) return std::nullopt;

    double slope = 2.0;
    double base_scale = points.back().first;
    double base_size = static_cast<double>(points.back().second);
    if (points.size() >= 2) {
        // 优先取包住目标的那一段；包不住就用最靠近目标的那一段外推
        std::size_t bracket = points.size();      // points.size() 表示"没找到"
        for (std::size_t index = 0; index + 1 < points.size(); ++index) {
            if (points[index].second <= target && target <= points[index + 1].second) {
                bracket = index;
                break;
            }
        }
        if (bracket == points.size()) {
            bracket = target < points.front().second ? 0 : points.size() - 2;
        }
        const double s1 = points[bracket].first;
        const double z1 = static_cast<double>(points[bracket].second);
        const double s2 = points[bracket + 1].first;
        const double z2 = static_cast<double>(points[bracket + 1].second);
        if (std::fabs(log_or_floor(s2) - log_or_floor(s1)) > 1e-6 &&
            std::fabs(log_or_floor(z2) - log_or_floor(z1)) > 1e-6) {
            slope = (log_or_floor(z2) - log_or_floor(z1)) /
                    (log_or_floor(s2) - log_or_floor(s1));
        }
        // 实测斜率偶尔很离谱（内容突变、几页超大图混在一起），夹到 1.2~3.0
        slope = std::min(std::max(slope, 1.2), 3.0);
        const bool use_upper = target >= points[bracket + 1].second;
        base_scale = use_upper ? points[bracket + 1].first : points[bracket].first;
        base_size = static_cast<double>(use_upper ? points[bracket + 1].second
                                                 : points[bracket].second);
    }

    const double scale =
        base_scale * std::pow(static_cast<double>(target) / base_size, 1.0 / slope);
    return std::min(smax, std::max(smin, scale));
}

std::int64_t aim_size(std::int64_t target, Strategy strategy) {
    const auto [low, high] = accept_range(target, strategy);
    // Python 的 int() 是向零截断；这里的上下界都非负，截断即取整
    return static_cast<std::int64_t>((low + high) / 2.0);
}

std::optional<int> predict_quality(const std::vector<std::pair<int, std::int64_t>>& points,
                                   std::int64_t target, int qmin, int qmax) {
    const std::set<QualityPoint> unique(points.begin(), points.end());
    const std::vector<QualityPoint> pts(unique.begin(), unique.end());
    if (pts.size() < 2) return std::nullopt;

    std::size_t bracket = pts.size();
    for (std::size_t index = 0; index + 1 < pts.size(); ++index) {
        const std::int64_t left = pts[index].second;
        const std::int64_t right = pts[index + 1].second;
        // 质量越高体积越大，但实测偶尔反过来，所以两个方向都认
        if ((left <= target && target <= right) || (right <= target && target <= left)) {
            bracket = index;
            break;
        }
    }
    if (bracket == pts.size()) {
        bracket = target < pts.front().second ? 0 : pts.size() - 2;
    }

    const double q1 = static_cast<double>(pts[bracket].first);
    const double z1 = static_cast<double>(pts[bracket].second);
    const double q2 = static_cast<double>(pts[bracket + 1].first);
    const double z2 = static_cast<double>(pts[bracket + 1].second);
    if (std::fabs(log_or_floor(q2) - log_or_floor(q1)) < 1e-6 ||
        std::fabs(log_or_floor(z2) - log_or_floor(z1)) < 1e-6) {
        return std::nullopt;
    }
    double slope = (log_or_floor(z2) - log_or_floor(z1)) /
                   (log_or_floor(q2) - log_or_floor(q1));
    slope = std::min(std::max(slope, 0.2), 4.0);

    const bool use_upper = target >= pts[bracket + 1].second;
    const double base_q =
        static_cast<double>(use_upper ? pts[bracket + 1].first : pts[bracket].first);
    const double base_z =
        static_cast<double>(use_upper ? pts[bracket + 1].second : pts[bracket].second);
    const double quality =
        base_q * std::pow(static_cast<double>(target) / base_z, 1.0 / slope);
    const double clamped =
        std::min(std::max(quality, static_cast<double>(qmin)), static_cast<double>(qmax));
    return static_cast<int>(round_half_even(clamped));
}

Decision plan_next(const std::vector<Trial>& trials, std::int64_t target,
                   Strategy strategy, double smin, double smax, int qmin, int qmax,
                   int base_quality) {
    std::vector<const Trial*> rendered;
    rendered.reserve(trials.size());
    for (const Trial& trial : trials) {
        if (trial.rendered()) rendered.push_back(&trial);
    }

    // 第一次什么都不用算：先在 72 DPI 标定一次，拿到第一个数据点
    if (rendered.empty()) {
        return Decision{std::min(std::max(1.0, smin), smax), base_quality, false, {}};
    }

    std::set<std::pair<double, int>> tried;
    for (const Trial* trial : rendered) {
        tried.insert({round_decimals(trial->scale, 4), trial->quality});
    }
    const std::int64_t aim = aim_size(target, strategy);

    // predict_scale 自己会滤掉没渲染过的记录，所以直接喂完整的 trials 即可
    const std::optional<double> predicted = predict_scale(trials, aim, smin, smax);
    if (!predicted.has_value()) {
        return Decision{0.0, base_quality, true, tr("没有可用的测算数据")};
    }

    // 顶到上下限就改用 JPEG 质量当副旋钮
    if (*predicted >= smax) {
        return bound_plan(smax, rendered, target, strategy, qmin, qmax, base_quality,
                          /*grow=*/true);
    }
    if (*predicted <= smin) {
        return bound_plan(smin, rendered, target, strategy, qmin, qmax, base_quality,
                          /*grow=*/false);
    }

    double scale = *predicted;

    // 算出来的 scale 与已试过的那一次几乎相同：再渲染一遍纯属浪费，改用夹逼
    const auto tried_at_base = [&](double value) {
        for (const auto& entry : tried) {
            if (entry.second == base_quality && same_scale(value, entry.first)) return true;
        }
        return false;
    };
    if (tried_at_base(scale)) {
        const Trial* lo = nullptr;      // 已经比 aim 小的记录里，scale 最大的那次
        const Trial* hi = nullptr;      // 还没到 aim 的记录里，scale 最小的那次
        for (const Trial* trial : rendered) {
            if (trial->size < aim) {
                if (lo == nullptr || trial->scale > lo->scale) lo = trial;
            } else {
                if (hi == nullptr || trial->scale < hi->scale) hi = trial;
            }
        }
        if (lo != nullptr && hi != nullptr && lo->scale > 0.0) {
            if (hi->scale / lo->scale > 1.005) {
                return Decision{std::sqrt(lo->scale * hi->scale), base_quality, false, {}};
            }
            return Decision{
                0.0, base_quality, true,
                tr("已把参数收缩到 {lo:.0f}~{hi:.0f} DPI，文件大小在 {lo_size} ~ {hi_size} 之间，"
                   "该策略要求的误差范围已无法再逼近",
                   {{"lo", fmt_fixed(scale_to_dpi(lo->scale), 0)},
                    {"hi", fmt_fixed(scale_to_dpi(hi->scale), 0)},
                    {"lo_size", human_size(static_cast<double>(lo->size))},
                    {"hi_size", human_size(static_cast<double>(hi->size))}})};
        }
        const Trial* last = rendered.back();
        scale = last->size < aim ? std::min(smax, scale * 1.08)
                                 : std::max(smin, scale * 0.92);
        if (tried_at_base(scale)) {
            return Decision{0.0, base_quality, true, tr("参数已无可调整空间")};
        }
    }
    return Decision{scale, base_quality, false, {}};
}

}  // namespace pdfc
