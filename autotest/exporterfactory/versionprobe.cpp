/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(int argc, char *argv[])
{
    if (argc != 2 || std::strcmp(argv[1], "--version") != 0) {
        return 1;
    }

    wchar_t executablePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return 2;
    }

    const std::filesystem::path executable(executablePath);
    std::ofstream log(executable.parent_path() / "probes.log", std::ios::app);
    if (!log) {
        return 3;
    }
    log << executable.stem().string() << '\n';
    std::puts("version 6.0.0");
    return 0;
}
