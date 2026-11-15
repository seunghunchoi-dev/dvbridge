// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// DMR 오류정정 부호 — ETSI TS 102 361-1 V2.6.1 Annex B 기준으로 새로 작성
//   Golay(20,8) B.3.1 / QR(16,7) B.3.2 / Hamming(13,9)(15,11)(16,11) B.3.4 / RS(12,9) B.3.6 / CRC 마스크 B.3.12 / CS B.3.11
// 부호어 비트 순서: 생성행렬의 첫 열 = 반환값의 최상위 비트(MSB)
#pragma once
#include <cstdint>

namespace dmr {

// 일반 선형 블록 부호 (생성행렬 문자열로 정의, k ≤ 16, n ≤ 32)
class LinearCode {
public:
	LinearCode(int n, int k, const char *const *rows);
	int n() const { return n_; }
	int k() const { return k_; }
	uint32_t encode(uint32_t info) const;					// info(k비트) → 부호어(n비트)
	// 최소거리 복호 (전수 탐색, k가 작을 때만). 반환: 정정한 비트 수, 정정 불가면 -1
	int decode_ml(uint32_t received, uint32_t &info, int max_errors) const;
	// 단일 오류 정정 (Hamming류): 반환 0=오류없음, 1=1비트 정정, -1=정정 불가
	int correct_single(uint32_t &codeword) const;
	uint32_t syndrome(uint32_t codeword) const;				// 0이면 부호어
private:
	int n_, k_;
	uint32_t row_[16];		// 각 정보 비트의 부호어 기여 (n비트)
	uint32_t err_syn_[32];	// 비트 j 단일 오류의 신드롬
};

const LinearCode &golay2087();
const LinearCode &qr1676();
const LinearCode &hamming1393();
const LinearCode &hamming15113();
const LinearCode &hamming16114();

// Reed-Solomon(12,9) over GF(2^8), p(x)=x^8+x^4+x^3+x^2+1 — LC 9바이트 → 패리티 3바이트
void rs129_parity(const uint8_t lc[9], uint8_t parity[3]);
bool rs129_check(const uint8_t lc[9], const uint8_t parity[3]);
uint8_t gf_mul(uint8_t a, uint8_t b);

// B.3.12 데이터 타입 CRC 마스크 (24비트)
constexpr uint32_t kMaskVoiceLcHeader = 0x969696;
constexpr uint32_t kMaskTerminatorLc  = 0x999999;

// B.3.11 임베디드 LC 5비트 체크섬
uint8_t embedded_checksum(const uint8_t lc[9]);

} // namespace dmr
