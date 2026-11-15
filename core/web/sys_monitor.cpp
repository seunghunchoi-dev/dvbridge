// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "sys_monitor.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static uint64_t ft(const FILETIME &f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
#endif

SysMonitor::SysMonitor()
{
	up_.start();
#ifdef _WIN32
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	cores_ = int(si.dwNumberOfProcessors);
#endif
	sample();
}

void SysMonitor::sample()
{
#ifdef _WIN32
	FILETIME c, e, k, u, now;
	GetSystemTimeAsFileTime(&now);
	const uint64_t wall = ft(now);
	if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
		const uint64_t p = ft(k) + ft(u);
		if (last_wall_ && wall > last_wall_) proc_cpu_ = 100.0 * double(p - last_proc_) / double(wall - last_wall_) / cores_;
		last_proc_ = p;
	}
	FILETIME idle, kern, user;
	if (GetSystemTimes(&idle, &kern, &user)) {
		const uint64_t i = ft(idle), kk = ft(kern), uu = ft(user);
		const uint64_t total = (kk - last_kernel_) + (uu - last_user_);	// 커널 시간에 유휴 시간 포함
		if (last_kernel_ && total) sys_cpu_ = 100.0 * double(total - (i - last_idle_)) / double(total);
		last_idle_ = i; last_kernel_ = kk; last_user_ = uu;
	}
	last_wall_ = wall;
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) proc_mem_mb_ = double(pmc.WorkingSetSize) / 1048576.0;
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms)) {
		sys_mem_used_pct_ = ms.dwMemoryLoad;
		sys_mem_total_gb_ = double(ms.ullTotalPhys) / 1073741824.0;
	}
#endif
	proc_cpu_ = qBound(0.0, proc_cpu_, 100.0);
	sys_cpu_ = qBound(0.0, sys_cpu_, 100.0);
}

QJsonObject SysMonitor::to_json() const
{
	QJsonObject o;
	o["proc_cpu"] = qRound(proc_cpu_ * 10) / 10.0;
	o["sys_cpu"] = qRound(sys_cpu_ * 10) / 10.0;
	o["proc_mem_mb"] = qRound(proc_mem_mb_ * 10) / 10.0;
	o["sys_mem_pct"] = sys_mem_used_pct_;
	o["sys_mem_gb"] = qRound(sys_mem_total_gb_ * 10) / 10.0;
	o["uptime_s"] = double(up_.elapsed() / 1000);
	o["cores"] = cores_;
	return o;
}
