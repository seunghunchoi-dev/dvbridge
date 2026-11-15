// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// SysMonitor — 서버 부하: DVBridge 프로세스·PC 전체 CPU 사용률, 메모리, 가동 시간
//   sample()을 주기적으로(예: 1초) 불러 CPU 사용률을 직전 표본과의 차이로 계산
#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <cstdint>

class SysMonitor
{
public:
	SysMonitor();
	void sample();
	QJsonObject to_json() const;
	double process_cpu() const { return proc_cpu_; }		// % (전체 코어 기준 0~100)
	double system_cpu() const { return sys_cpu_; }
private:
	QElapsedTimer up_;
	uint64_t last_proc_ = 0, last_idle_ = 0, last_kernel_ = 0, last_user_ = 0, last_wall_ = 0;
	double proc_cpu_ = 0, sys_cpu_ = 0;
	double proc_mem_mb_ = 0, sys_mem_used_pct_ = 0, sys_mem_total_gb_ = 0;
	int cores_ = 1;
};
