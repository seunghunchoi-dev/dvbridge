// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// ETSI TS 102 361-1 Annex B 기준 오류정정 부호 (설명은 fec.h)
#include "fec.h"
#include <cstring>

namespace dmr {

static int popcount32(uint32_t v) { int c = 0; while (v) { v &= v - 1; ++c; } return c; }

LinearCode::LinearCode(int n, int k, const char *const *rows) : n_(n), k_(k)
{
	for (int i = 0; i < k; ++i) {
		uint32_t r = 0;
		const char *s = rows[i];
		int bits = 0;
		for (; *s && bits < n; ++s) {
			if (*s == '0' || *s == '1') { r = (r << 1) | uint32_t(*s - '0'); ++bits; }
		}
		row_[i] = r;
	}
	// 단일 오류 신드롬: 정보 비트 j 오류 → 그 행의 패리티, 패리티 비트 오류 → 단위 벡터
	const int pbits = n - k;
	const uint32_t pmask = (1u << pbits) - 1u;
	for (int j = 0; j < n; ++j) {
		if (j < k) err_syn_[j] = row_[j] & pmask;
		else err_syn_[j] = 1u << (n - 1 - j);
	}
}

uint32_t LinearCode::encode(uint32_t info) const
{
	uint32_t c = 0;
	for (int i = 0; i < k_; ++i) {
		if (info & (1u << (k_ - 1 - i))) c ^= row_[i];
	}
	return c;
}

uint32_t LinearCode::syndrome(uint32_t cw) const
{
	const int pbits = n_ - k_;
	const uint32_t info = cw >> pbits;
	return (encode(info) ^ cw) & ((1u << pbits) - 1u);
}

int LinearCode::correct_single(uint32_t &cw) const
{
	const uint32_t s = syndrome(cw);
	if (!s) return 0;
	for (int j = 0; j < n_; ++j) {
		if (err_syn_[j] == s) { cw ^= 1u << (n_ - 1 - j); return 1; }
	}
	return -1;
}

int LinearCode::decode_ml(uint32_t received, uint32_t &info, int max_errors) const
{
	int best = 99;
	uint32_t best_info = 0;
	const uint32_t mask = (n_ == 32) ? 0xFFFFFFFFu : ((1u << n_) - 1u);
	for (uint32_t m = 0; m < (1u << k_); ++m) {
		const int d = popcount32((encode(m) ^ received) & mask);
		if (d < best) { best = d; best_info = m; if (!d) break; }
	}
	if (best > max_errors) return -1;
	info = best_info;
	return best;
}

// ---- 표준 생성행렬 (ETSI TS 102 361-1 V2.6.1 표 B.11, B.12, B.14, B.15, B.16 그대로) ----
static const char *const kGolay2087[8] = {
	"1 0 0 0 0 0 0 0 0 0 1 1 1 1 0 1 1 0 1 0",
	"0 1 0 0 0 0 0 0 1 1 0 1 1 0 0 1 1 0 0 1",
	"0 0 1 0 0 0 0 0 0 1 1 0 1 1 0 0 1 1 0 1",
	"0 0 0 1 0 0 0 0 0 0 1 1 0 1 1 0 0 1 1 1",
	"0 0 0 0 1 0 0 0 1 1 0 1 1 1 0 0 0 1 1 0",
	"0 0 0 0 0 1 0 0 1 0 1 0 1 0 0 1 0 1 1 1",
	"0 0 0 0 0 0 1 0 1 0 0 1 0 0 1 1 1 1 1 0",
	"0 0 0 0 0 0 0 1 1 0 0 0 1 1 1 0 1 0 1 1",
};
static const char *const kQr1676[7] = {
	"1 0 0 0 0 0 0 0 0 1 0 0 1 1 1 1",
	"0 1 0 0 0 0 0 1 0 0 0 1 1 1 1 0",
	"0 0 1 0 0 0 0 1 1 0 1 1 0 1 1 1",
	"0 0 0 1 0 0 0 1 1 1 1 0 0 0 1 0",
	"0 0 0 0 1 0 0 1 1 1 0 0 1 0 0 1",
	"0 0 0 0 0 1 0 0 1 1 1 0 0 1 0 1",
	"0 0 0 0 0 0 1 0 0 1 1 1 0 0 1 1",
};
static const char *const kHamming1393[9] = {
	"1 0 0 0 0 0 0 0 0 1 1 1 1",
	"0 1 0 0 0 0 0 0 0 1 1 1 0",
	"0 0 1 0 0 0 0 0 0 0 1 1 1",
	"0 0 0 1 0 0 0 0 0 1 0 1 0",
	"0 0 0 0 1 0 0 0 0 0 1 0 1",
	"0 0 0 0 0 1 0 0 0 1 0 1 1",
	"0 0 0 0 0 0 1 0 0 1 1 0 0",
	"0 0 0 0 0 0 0 1 0 0 1 1 0",
	"0 0 0 0 0 0 0 0 1 0 0 1 1",
};
static const char *const kHamming15113[11] = {
	"1 0 0 0 0 0 0 0 0 0 0 1 0 0 1",
	"0 1 0 0 0 0 0 0 0 0 0 1 1 0 1",
	"0 0 1 0 0 0 0 0 0 0 0 1 1 1 1",
	"0 0 0 1 0 0 0 0 0 0 0 1 1 1 0",
	"0 0 0 0 1 0 0 0 0 0 0 0 1 1 1",
	"0 0 0 0 0 1 0 0 0 0 0 1 0 1 0",
	"0 0 0 0 0 0 1 0 0 0 0 0 1 0 1",
	"0 0 0 0 0 0 0 1 0 0 0 1 0 1 1",
	"0 0 0 0 0 0 0 0 1 0 0 1 1 0 0",
	"0 0 0 0 0 0 0 0 0 1 0 0 1 1 0",
	"0 0 0 0 0 0 0 0 0 0 1 0 0 1 1",
};
static const char *const kHamming16114[11] = {
	"1 0 0 0 0 0 0 0 0 0 0 1 0 0 1 1",
	"0 1 0 0 0 0 0 0 0 0 0 1 1 0 1 0",
	"0 0 1 0 0 0 0 0 0 0 0 1 1 1 1 1",
	"0 0 0 1 0 0 0 0 0 0 0 1 1 1 0 0",
	"0 0 0 0 1 0 0 0 0 0 0 0 1 1 1 0",
	"0 0 0 0 0 1 0 0 0 0 0 1 0 1 0 1",
	"0 0 0 0 0 0 1 0 0 0 0 0 1 0 1 1",
	"0 0 0 0 0 0 0 1 0 0 0 1 0 1 1 0",
	"0 0 0 0 0 0 0 0 1 0 0 1 1 0 0 1",
	"0 0 0 0 0 0 0 0 0 1 0 0 1 1 0 1",
	"0 0 0 0 0 0 0 0 0 0 1 0 0 1 1 1",
};

const LinearCode &golay2087()   { static const LinearCode c(20, 8, kGolay2087); return c; }
const LinearCode &qr1676()      { static const LinearCode c(16, 7, kQr1676); return c; }
const LinearCode &hamming1393() { static const LinearCode c(13, 9, kHamming1393); return c; }
const LinearCode &hamming15113(){ static const LinearCode c(15, 11, kHamming15113); return c; }
const LinearCode &hamming16114(){ static const LinearCode c(16, 11, kHamming16114); return c; }

// ---- GF(2^8), 원시다항식 x^8+x^4+x^3+x^2+1 (B.14), 지수/로그표는 프로그램이 직접 계산 ----
namespace {
struct GF {
	uint8_t exp[512];
	uint8_t log[256];
	GF() {
		unsigned x = 1;
		for (int i = 0; i < 255; ++i) {
			exp[i] = uint8_t(x);
			log[x] = uint8_t(i);
			x <<= 1;
			if (x & 0x100) x ^= 0x11D;
		}
		for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
		log[0] = 0;
	}
};
const GF &gf() { static const GF g; return g; }
}

uint8_t gf_mul(uint8_t a, uint8_t b)
{
	if (!a || !b) return 0;
	return gf().exp[gf().log[a] + gf().log[b]];
}

// g(x) = (x+α)(x+α²)(x+α³) = x³ + 0E x² + 38 x + 40 (B.10) — 계수는 곱셈으로 직접 구함
void rs129_parity(const uint8_t lc[9], uint8_t parity[3])
{
	// c[d] = x^d 계수. 1에서 시작해 (x + α^r)를 r=1..3 곱함
	uint8_t c[4] = {1, 0, 0, 0};
	for (int r = 1; r <= 3; ++r) {
		const uint8_t a = gf().exp[r];
		uint8_t res[4] = {0, 0, 0, 0};
		for (int d = 0; d < 4; ++d) {
			if (!c[d]) continue;
			if (d + 1 < 4) res[d + 1] ^= c[d];	// × x
			res[d] ^= gf_mul(c[d], a);			// × α^r
		}
		std::memcpy(c, res, 4);
	}
	const uint8_t g[4] = {c[3], c[2], c[1], c[0]};	// g[0]=x³(=1) … g[3]=상수항 (기대값 01 0E 38 40)
	// 체계적 부호화: m(x)·x³ mod g(x) (LFSR). lc[0]이 최고차
	uint8_t reg[3] = {0, 0, 0};
	for (int i = 0; i < 9; ++i) {
		const uint8_t fb = lc[i] ^ reg[0];
		reg[0] = reg[1] ^ gf_mul(fb, g[1]);
		reg[1] = reg[2] ^ gf_mul(fb, g[2]);
		reg[2] = gf_mul(fb, g[3]);
	}
	parity[0] = reg[0];	// p2
	parity[1] = reg[1];	// p1
	parity[2] = reg[2];	// p0
}

bool rs129_check(const uint8_t lc[9], const uint8_t parity[3])
{
	uint8_t p[3];
	rs129_parity(lc, p);
	return p[0] == parity[0] && p[1] == parity[1] && p[2] == parity[2];
}

uint8_t embedded_checksum(const uint8_t lc[9])
{
	unsigned sum = 0;
	for (int i = 0; i < 9; ++i) sum += lc[i];
	return uint8_t(sum % 31u);
}

} // namespace dmr
