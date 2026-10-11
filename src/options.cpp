// -*- coding: utf-8 -*-
//
// 选项解析与校验。

#include "options.hpp"

#include <algorithm>

#include "common.hpp"
#include "i18n.hpp"

namespace pdfc {

int resolve_workers(int jobs) {
    // 上限 64 与 Python 版一致：再多也不会有收益，只会把内存吃光
    if (jobs > 0) return std::max(1, std::min(jobs, 64));
    return std::max(1, physical_cpu_count());
}

std::string validate_options(const JobOptions& options) {
    if (options.dpi.has_value()) {
        if (*options.dpi <= 0.0) {
            return tr("--dpi 必须大于 0");
        }
    } else if (!options.target_size.has_value()) {
        return tr("需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率");
    } else if (*options.target_size <= 0.0) {
        return tr("目标大小必须大于 0");
    }

    if (options.min_dpi >= options.max_dpi) {
        return tr("--min-dpi({min:g}) 必须小于 --max-dpi({max:g})",
                  {{"min", fmt_general(options.min_dpi)},
                   {"max", fmt_general(options.max_dpi)}});
    }
    if (options.max_retries < 1) {
        return tr("--max-retries 必须大于 0");
    }
    return {};
}

}  // namespace pdfc
