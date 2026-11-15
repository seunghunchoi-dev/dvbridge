// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// DMR 버스트 조립·해체 (설명은 burst.h)
#include "burst.h"
#include "fec.h"
#include <cstring>

namespace dmr {

// ===================== Full LC =====================
void FullLc::to_bytes(uint8_t o[9]) const
{
	o[0] = uint8_t(flco & 0x3F);		// PF=0, R=0 (9.3.10)
	o[1] = fid;
	o[2] = service;
	o[3] = uint8_t(dst >> 16); o[4] = uint8_t(dst >> 8); o[5] = uint8_t(dst);
	o[6] = uint8_t(src >> 16); o[7] = uint8_t(src >> 8); o[8] = uint8_t(src);
}

FullLc FullLc::from_bytes(const uint8_t i[9])
{
	FullLc lc;
	lc.flco = i[0] & 0x3F;
	lc.fid = i[1];
	lc.service = i[2];
	lc.dst = (uint32_t(i[3]) << 16) | (uint32_t(i[4]) << 8) | i[5];
	lc.src = (uint32_t(i[6]) << 16) | (uint32_t(i[7]) << 8) | i[8];
	return lc;
}

// ===================== BPTC(196,96) =====================
// 부호화 행렬 13행 × 15열, 행렬 비트 (r,c)의 순번 = 1 + r*15 + c (순번 0 = R(3)) — 표 B.2
// 0행: R(2) R(1) R(0) I(95)…I(88) | 1~8행: 정보 11비트 | 각 행 끝 4비트 = Hamming(15,11)
// 9~12행: 15개 열 각각의 Hamming(13,9) 패리티
// 인터리브 위치 = 순번 × 181 mod 196 (식 B.1)
static void bptc_matrix_from_info(const uint8_t in12[12], bool m[13][15])
{
	std::memset(m, 0, sizeof(bool) * 13 * 15);
	int info = 95;	// I(95)부터
	for (int r = 0; r < 9; ++r) {
		for (int c = (r == 0 ? 3 : 0); c < 11; ++c, --info) {
			const int bit = 95 - info;	// I(95)=in12 첫 비트
			m[r][c] = get_bit(in12, bit);
		}
	}
}

void bptc19696_encode(const uint8_t in12[12], bool out196[196])
{
	bool m[13][15];
	bptc_matrix_from_info(in12, m);
	const LinearCode &rowc = hamming15113();
	const LinearCode &colc = hamming1393();
	for (int r = 0; r < 9; ++r) {
		uint32_t v = 0;
		for (int c = 0; c < 11; ++c) v = (v << 1) | m[r][c];
		const uint32_t cw = rowc.encode(v);
		for (int c = 0; c < 15; ++c) m[r][c] = (cw >> (14 - c)) & 1;
	}
	for (int c = 0; c < 15; ++c) {
		uint32_t v = 0;
		for (int r = 0; r < 9; ++r) v = (v << 1) | m[r][c];
		const uint32_t cw = colc.encode(v);
		for (int r = 0; r < 13; ++r) m[r][c] = (cw >> (12 - r)) & 1;
	}
	bool lin[196];
	lin[0] = false;	// R(3)
	for (int r = 0; r < 13; ++r)
		for (int c = 0; c < 15; ++c) lin[1 + r * 15 + c] = m[r][c];
	for (int i = 0; i < 196; ++i) out196[(i * 181) % 196] = lin[i];
}

int bptc19696_decode(const bool in196[196], uint8_t out12[12])
{
	bool lin[196];
	for (int i = 0; i < 196; ++i) lin[i] = in196[(i * 181) % 196];
	bool m[13][15];
	for (int r = 0; r < 13; ++r)
		for (int c = 0; c < 15; ++c) m[r][c] = lin[1 + r * 15 + c];

	const LinearCode &rowc = hamming15113();
	const LinearCode &colc = hamming1393();
	int fixed = 0;
	bool bad = false;
	for (int pass = 0; pass < 4; ++pass) {
		bool changed = false;
		bad = false;
		for (int c = 0; c < 15; ++c) {
			uint32_t v = 0;
			for (int r = 0; r < 13; ++r) v = (v << 1) | m[r][c];
			const int res = colc.correct_single(v);
			if (res < 0) { bad = true; continue; }
			if (res > 0) {
				++fixed; changed = true;
				for (int r = 0; r < 13; ++r) m[r][c] = (v >> (12 - r)) & 1;
			}
		}
		for (int r = 0; r < 9; ++r) {
			uint32_t v = 0;
			for (int c = 0; c < 15; ++c) v = (v << 1) | m[r][c];
			const int res = rowc.correct_single(v);
			if (res < 0) { bad = true; continue; }
			if (res > 0) {
				++fixed; changed = true;
				for (int c = 0; c < 15; ++c) m[r][c] = (v >> (14 - c)) & 1;
			}
		}
		if (!changed) break;
	}
	if (bad) return -1;
	std::memset(out12, 0, 12);
	int info = 95;
	for (int r = 0; r < 9; ++r)
		for (int c = (r == 0 ? 3 : 0); c < 11; ++c, --info) set_bit(out12, 95 - info, m[r][c]);
	return fixed;
}

// ===================== 버스트 필드 위치 =====================
static void put_bits(Burst &b, int pos, uint64_t v, int n)
{
	for (int i = 0; i < n; ++i) set_bit(b.data(), pos + i, (v >> (n - 1 - i)) & 1);
}
static uint64_t take_bits(const Burst &b, int pos, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; ++i) v = (v << 1) | get_bit(b.data(), pos + i);
	return v;
}

uint64_t burst_center(const Burst &b) { return take_bits(b, 108, 48); }

// ===================== 헤더 / 종료 버스트 =====================
Burst build_lc_burst(const FullLc &lc, DataType dt, uint8_t cc, uint64_t sync)
{
	uint8_t payload[12];
	lc.to_bytes(payload);
	uint8_t parity[3];
	rs129_parity(payload, parity);
	const uint32_t mask = (dt == DT_VOICE_LC_HEADER) ? kMaskVoiceLcHeader : kMaskTerminatorLc;
	payload[9]  = parity[0] ^ uint8_t(mask >> 16);
	payload[10] = parity[1] ^ uint8_t(mask >> 8);
	payload[11] = parity[2] ^ uint8_t(mask);

	bool bits[196];
	bptc19696_encode(payload, bits);
	Burst b{};
	for (int i = 0; i < 98; ++i) set_bit(b.data(), i, bits[i]);
	for (int i = 0; i < 98; ++i) set_bit(b.data(), 166 + i, bits[98 + i]);
	const uint32_t slot = golay2087().encode(uint32_t(((cc & 0xF) << 4) | (dt & 0xF)));
	put_bits(b, 98, slot >> 10, 10);
	put_bits(b, 156, slot & 0x3FF, 10);
	put_bits(b, 108, sync, 48);
	return b;
}

bool parse_lc_burst(const Burst &b, FullLc &lc, DataType &dt, uint8_t &cc)
{
	const uint32_t slot = uint32_t((take_bits(b, 98, 10) << 10) | take_bits(b, 156, 10));
	uint32_t info = 0;
	if (golay2087().decode_ml(slot, info, 3) < 0) return false;
	cc = uint8_t(info >> 4);
	dt = DataType(info & 0xF);
	if (dt != DT_VOICE_LC_HEADER && dt != DT_TERMINATOR_WITH_LC) return false;
	bool bits[196];
	for (int i = 0; i < 98; ++i) bits[i] = get_bit(b.data(), i);
	for (int i = 0; i < 98; ++i) bits[98 + i] = get_bit(b.data(), 166 + i);
	uint8_t payload[12];
	if (bptc19696_decode(bits, payload) < 0) return false;
	const uint32_t mask = (dt == DT_VOICE_LC_HEADER) ? kMaskVoiceLcHeader : kMaskTerminatorLc;
	const uint8_t parity[3] = {uint8_t(payload[9] ^ (mask >> 16)), uint8_t(payload[10] ^ (mask >> 8)), uint8_t(payload[11] ^ mask)};
	if (!rs129_check(payload, parity)) return false;	// (오류정정은 BPTC가 담당, RS는 확인용)
	lc = FullLc::from_bytes(payload);
	return true;
}

// ===================== 임베디드 LC =====================
// 부호화 행렬 8행 × 16열 (그림 B.3)
//   0행 LC71..61 | 1행 LC60..50 | 2~6행: LC 10비트 + CS 1비트(10열, CS4→CS0)
//   각 행 끝 5비트 Hamming(16,11) | 7행 = 열 짝수 패리티
// 전송: 열을 위→아래, 왼쪽→오른쪽 순서로 읽어 128비트 → 32비트씩 4조각
void embedded_lc_encode(const FullLc &lc, uint32_t frag[4])
{
	uint8_t b[9];
	lc.to_bytes(b);
	embedded_lc_encode_bytes(b, frag);
}

void embedded_lc_encode_bytes(const uint8_t b[9], uint32_t frag[4])
{
	const uint8_t cs = embedded_checksum(b);
	bool m[8][16] = {};
	int lcbit = 0;	// 0 = LC(71)
	for (int r = 0; r < 7; ++r) {
		const int n = (r < 2) ? 11 : 10;
		for (int c = 0; c < n; ++c, ++lcbit) m[r][c] = get_bit(b, lcbit);
		if (r >= 2) m[r][10] = (cs >> (4 - (r - 2))) & 1;
		uint32_t v = 0;
		for (int c = 0; c < 11; ++c) v = (v << 1) | m[r][c];
		const uint32_t cw = hamming16114().encode(v);
		for (int c = 0; c < 16; ++c) m[r][c] = (cw >> (15 - c)) & 1;
	}
	for (int c = 0; c < 16; ++c) {
		bool p = false;
		for (int r = 0; r < 7; ++r) p ^= m[r][c];
		m[7][c] = p;
	}
	for (int f = 0; f < 4; ++f) frag[f] = 0;
	for (int k = 0; k < 128; ++k) {
		const bool v = m[k % 8][k / 8];
		frag[k / 32] = (frag[k / 32] << 1) | (v ? 1u : 0u);
	}
}

bool embedded_lc_decode(const uint32_t frag[4], FullLc &lc)
{
	uint8_t b[9];
	if (!embedded_lc_decode_bytes(frag, b)) return false;
	lc = FullLc::from_bytes(b);
	return true;
}

bool embedded_lc_decode_bytes(const uint32_t frag[4], uint8_t out[9])
{
	bool m[8][16];
	for (int k = 0; k < 128; ++k) m[k % 8][k / 8] = (frag[k / 32] >> (31 - (k % 32))) & 1;
	for (int r = 0; r < 7; ++r) {
		uint32_t v = 0;
		for (int c = 0; c < 16; ++c) v = (v << 1) | m[r][c];
		if (hamming16114().correct_single(v) < 0) return false;
		for (int c = 0; c < 16; ++c) m[r][c] = (v >> (15 - c)) & 1;
	}
	// 열 패리티(7행)는 검사하지 않음: 7행 자체에 오류가 나도 1~6행 Hamming 정정 + 체크섬으로 충분
	uint8_t b[9] = {};
	int lcbit = 0;
	uint8_t cs = 0;
	for (int r = 0; r < 7; ++r) {
		const int n = (r < 2) ? 11 : 10;
		for (int c = 0; c < n; ++c, ++lcbit) set_bit(b, lcbit, m[r][c]);
		if (r >= 2) cs = uint8_t((cs << 1) | m[r][10]);
	}
	if (cs != embedded_checksum(b)) return false;
	for (int i = 0; i < 9; ++i) out[i] = b[i];
	return true;
}

// ===================== EMB =====================
uint16_t emb_encode(uint8_t cc, bool pi, Lcss lcss)
{
	return uint16_t(qr1676().encode(uint32_t(((cc & 0xF) << 3) | (pi ? 4 : 0) | (lcss & 3))));
}

bool emb_decode(uint16_t emb, uint8_t &cc, bool &pi, Lcss &lcss)
{
	uint32_t info = 0;
	if (qr1676().decode_ml(emb, info, 2) < 0) return false;
	cc = uint8_t(info >> 3);
	pi = (info >> 2) & 1;
	lcss = Lcss(info & 3);
	return true;
}

// ===================== 음성 버스트 =====================
// 보코더 프레임 3개(VF1~VF3, 각 72비트) = VS(215)…VS(0) (6.1절)
//   버스트 0~107 = VS(215..108), 156~263 = VS(107..0)
Burst build_voice_burst(const AmbeFrame f[3], int n, uint8_t cc, const uint32_t lc_frag[4], uint64_t sync)
{
	Burst b{};
	for (int i = 0; i < 216; ++i) {
		const bool v = get_bit(f[i / 72].data(), i % 72);
		set_bit(b.data(), i < 108 ? i : i + 48, v);
	}
	if (n == 0) {
		put_bits(b, 108, sync, 48);
	} else {
		Lcss lcss = LCSS_SINGLE;
		uint32_t emb_payload = 0;		// F 버스트: 널 임베디드
		if (n >= 1 && n <= 4) {
			lcss = (n == 1) ? LCSS_FIRST : (n == 4) ? LCSS_LAST : LCSS_CONT;
			emb_payload = lc_frag[n - 1];
		}
		const uint16_t emb = emb_encode(cc, false, lcss);
		put_bits(b, 108, emb >> 8, 8);
		put_bits(b, 116, emb_payload, 32);
		put_bits(b, 148, emb & 0xFF, 8);
	}
	return b;
}

void extract_ambe(const Burst &b, AmbeFrame f[3])
{
	for (int k = 0; k < 3; ++k) f[k].fill(0);
	for (int i = 0; i < 216; ++i) {
		const bool v = get_bit(b.data(), i < 108 ? i : i + 48);
		set_bit(f[i / 72].data(), i % 72, v);
	}
}

} // namespace dmr
