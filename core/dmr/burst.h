// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// DMR 버스트(33바이트 = 264비트) 조립·해체 — ETSI TS 102 361-1 기준
//   데이터 버스트: 정보98 | 슬롯타입10 | SYNC 48 | 슬롯타입10 | 정보98   (6.2절)
//   음성 버스트:   음성108 | SYNC 또는 EMB8+임베디드32+EMB8 | 음성108   (6.1절)
// 비트 번호: 버스트 첫 바이트의 MSB = 0번
#pragma once
#include <array>
#include <cstdint>

namespace dmr {

using Burst = std::array<uint8_t, 33>;
using AmbeFrame = std::array<uint8_t, 9>;	// 72비트 (보코더 플러그인 출력 그대로)

// 표 9.2 동기 패턴 (48비트)
constexpr uint64_t kSyncBsVoice = 0x755FD7DF75F7ULL;
constexpr uint64_t kSyncBsData  = 0xDFF57D75DF5DULL;
constexpr uint64_t kSyncMsVoice = 0x7F7D5DD57DFDULL;
constexpr uint64_t kSyncMsData  = 0xD5D7F77FD757ULL;

// 표 9.22 데이터 타입
enum DataType : uint8_t { DT_VOICE_LC_HEADER = 1, DT_TERMINATOR_WITH_LC = 2 };
// LCSS (EMB): 0=단일조각/없음, 1=첫 조각, 2=마지막 조각, 3=이어짐
enum Lcss : uint8_t { LCSS_SINGLE = 0, LCSS_FIRST = 1, LCSS_LAST = 2, LCSS_CONT = 3 };

// ---- 비트 도구 ----
inline bool get_bit(const uint8_t *b, int i) { return (b[i >> 3] >> (7 - (i & 7))) & 1; }
inline void set_bit(uint8_t *b, int i, bool v)
{
	if (v) b[i >> 3] |= uint8_t(0x80 >> (i & 7)); else b[i >> 3] &= uint8_t(~(0x80 >> (i & 7)));
}

// ---- Full LC (7.1절, FLCO는 TS 102 361-2) ----
struct FullLc {
	uint8_t flco = 0;		// 0 = 그룹 음성, 3 = 개인(유닛 간) 음성
	uint8_t fid = 0;		// 0 = 표준
	uint8_t service = 0;	// 서비스 옵션
	uint32_t dst = 0;		// 24비트
	uint32_t src = 0;		// 24비트
	void to_bytes(uint8_t out[9]) const;
	static FullLc from_bytes(const uint8_t in[9]);
	bool is_private() const { return flco == 3; }
};
constexpr uint8_t kFlcoGroup = 0;
constexpr uint8_t kFlcoPrivate = 3;

// ---- BPTC(196,96) (B.1) : 96비트(12바이트) ↔ 196비트 인터리브 배열 ----
void bptc19696_encode(const uint8_t in12[12], bool out196[196]);
// 반환: 정정한 비트 수(≥0), 정정 불가 -1
int bptc19696_decode(const bool in196[196], uint8_t out12[12]);

// ---- 데이터 버스트 (헤더/종료) ----
// 기본 동기 = MS 쪽 (BM이 실제로 받아들인 Homebrew 패킷과 같은 형식, 2026-11-14 확인)
Burst build_lc_burst(const FullLc &lc, DataType dt, uint8_t cc, uint64_t sync = kSyncMsData);
// 데이터 버스트 해석: 슬롯타입·LC 복호 (RS 확인 포함). 실패 시 false
bool parse_lc_burst(const Burst &b, FullLc &lc, DataType &dt, uint8_t &cc);

// ---- 임베디드 LC (B.2.1, 7.1.3) : LC 9바이트 → 32비트 조각 4개 (B~E 버스트용) ----
void embedded_lc_encode(const FullLc &lc, uint32_t frag[4]);
bool embedded_lc_decode(const uint32_t frag[4], FullLc &lc);
// 같은 부호화를 LC 9바이트 그대로 (Talker Alias 등 음성 LC가 아닌 내장 LC용, TS 102 361-2 7.1.1.4/7.1.1.5)
void embedded_lc_encode_bytes(const uint8_t lc9[9], uint32_t frag[4]);
bool embedded_lc_decode_bytes(const uint32_t frag[4], uint8_t lc9[9]);

// ---- EMB (9.1.2) : 색상코드·PI·LCSS → 16비트 ----
uint16_t emb_encode(uint8_t cc, bool pi, Lcss lcss);
bool emb_decode(uint16_t emb, uint8_t &cc, bool &pi, Lcss &lcss);

// ---- 음성 버스트 ----
// n: 0=A(SYNC) 1..5=B..F. B~E는 임베디드 LC 조각, F는 널 임베디드
Burst build_voice_burst(const AmbeFrame f[3], int n, uint8_t cc, const uint32_t lc_frag[4], uint64_t sync = kSyncMsVoice);
void extract_ambe(const Burst &b, AmbeFrame f[3]);

uint64_t burst_center(const Burst &b);	// 가운데 48비트 (SYNC 또는 EMB+임베디드)

} // namespace dmr
