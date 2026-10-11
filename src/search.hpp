// -*- coding: utf-8 -*-
//
// 目标大小搜索：纯数学，不碰文件，也不认识界面。
//
// 这一层是 Python 版 pdfcomp/core.py 里 search 那一段的直译，算法一字未改：
// 渲染后文件大小近似满足 size ∝ scale^p，在 log-log 空间用实测两点插值/外推，
// 通常 2~3 次渲染就能命中目标；只有 scale 顶到上下限时才改用 JPEG 质量做副旋钮。
//
// 没有任何全局状态，所以 tests/ 里可以直接喂假数据验证（不需要真的渲染 PDF）。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "options.hpp"

namespace pdfc {

// 搜索用到的常量（与 Python 版逐字相同）
inline constexpr int kQualityDefault = 85;
inline constexpr int kQualityMin = 50;
inline constexpr int kQualityMax = 100;
inline constexpr double kAcceptClosest = 0.05;   // closest：误差 ±5%
inline constexpr double kAcceptSmaller = 0.10;   // smaller：低于目标且误差 ≤10%

/// 一次尝试的记录。scale = 0 表示无损优化的结果。
struct Trial {
    double scale = 0.0;
    int quality = 0;
    std::int64_t size = 0;
    std::string path;
    bool is_input = false;      // 结果就是输入文件本身（无损没有收益时）

    bool rendered() const { return scale > 0.0; }
    double dpi() const;
};

/// 搜索的下一步：要么给出参数，要么说明无法达到。
struct Decision {
    double scale = 0.0;
    int quality = kQualityDefault;
    bool stop = false;
    std::string reason;         // 已翻译好的说明（stop 为真时给用户看）
};

/// 当前策略要求的合格区间 [下界, 上界)。
std::pair<double, double> accept_range(std::int64_t target, Strategy strategy);

bool is_accepted(std::int64_t size, std::int64_t target, Strategy strategy);

/// 按策略挑最佳结果；trials 为空返回 nullptr。
const Trial* pick_best(const std::vector<Trial>& trials, std::int64_t target,
                       Strategy strategy);

/// 用已测得的数据点推算能达到目标的 scale；数据不足返回 nullopt。
std::optional<double> predict_scale(const std::vector<Trial>& trials,
                                    std::int64_t target, double smin, double smax);

/// 搜索时瞄准的大小 = 合格区间的中心。
///
/// closest 的中心就是 target；但 smaller 的区间是 [0.9t, t)，瞄准 t 会一直贴在
/// 区间边界上来回（实测卡在 8.01MB / 8.00MB），瞄准中心 0.95t 才能一次落进去。
std::int64_t aim_size(std::int64_t target, Strategy strategy);

/// 在固定 scale 下用已测的 (quality, size) 点推算质量；数据不足返回 nullopt。
std::optional<int> predict_quality(const std::vector<std::pair<int, std::int64_t>>& points,
                                   std::int64_t target, int qmin, int qmax);

/// 给出下一次尝试的参数，或判断目标不可达。
Decision plan_next(const std::vector<Trial>& trials, std::int64_t target,
                   Strategy strategy, double smin, double smax,
                   int qmin = kQualityMin, int qmax = kQualityMax,
                   int base_quality = kQualityDefault);

}  // namespace pdfc
