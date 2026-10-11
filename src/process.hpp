// -*- coding: utf-8 -*-
//
// 子进程管理。渲染是纯 CPU 密集任务，用多进程而不是多线程：
// MuPDF 的单份文档不是线程安全的，一个进程一份文档最省心，也顺便绕开了
// 内存分配器在多线程下的争用。
//
// 子进程就是本程序自己，带一个 --worker 参数；规格通过临时文件传递，
// 免得几百页的页表把命令行撑爆（Windows 上限 32767 个字符）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pdfc {

/// 一个正在运行的子进程。句柄是不透明的，析构时由调用方显式收尾。
struct ChildProcess {
    void* handle = nullptr;      // Windows: HANDLE；POSIX: pid 放在 pid 里
    long long pid = 0;
    bool running = false;
};

/// 本进程的可执行文件全路径（子进程用它重新启动自己）。
std::string current_executable_path();

/**
 * 启动一个子进程。
 *
 * args 不含程序名（第一个元素由函数自己补上）；silent 为真时不创建控制台窗口
 * （Windows 上 GUI 程序调子进程必须这样，否则会闪黑框）。
 */
bool spawn_child(const std::vector<std::string>& args, ChildProcess& child,
                 std::string& error);

/// 等待结束，返回退出码。wait 返回假表示等待本身失败。
bool wait_child(ChildProcess& child, int& exit_code, std::string& error);

/// 强制结束（用户取消时用）。子进程正在写临时文件，所以父进程随后要自己清理。
void terminate_child(ChildProcess& child);

/// 关闭句柄，不结束进程。
void close_child(ChildProcess& child);

}  // namespace pdfc
