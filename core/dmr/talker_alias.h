// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// Talker Alias — 송신자 이름표 (ETSI TS 102 361-2 V2.5.1: 5.4.3, 7.1.1.4, 7.1.1.5, 7.2.18, 7.2.19)
//   음성 슈퍼프레임의 내장 LC(B~E)로 전달. 머리 LC(FLCO 4) + 블록 LC(FLCO 5·6·7) 최대 4개, 31 코드 단위
//   머리: PF(1) R(1) FLCO(6) | FID(8) | 형식(2) 길이(5) 자료(49)   블록: PF R FLCO | FID | 자료(56)
//   형식: 0 = 7비트 ASCII(머리 7자 + 블록 8자씩), 1 = ISO 8비트, 2 = UTF-8, 3 = UTF-16BE (8·16비트는 머리 자료 첫 비트 예약)
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dmr {

constexpr uint8_t kFlcoTalkerAliasHdr = 4;
constexpr uint8_t kFlcoTalkerAliasBlk1 = 5;		// 블록 2·3 = 6·7

// 송신: 7비트 ASCII로 부호화 (ASCII 밖 문자는 '?', 31자 넘으면 자름) → LC 9바이트 1~4개
std::vector<std::array<uint8_t, 9>> talker_alias_encode(const std::string &text);

// 수신: 내장 LC를 하나씩 넣으면 모두 모였을 때 글자를 돌려줌 (UTF-8)
class TalkerAliasRx {
public:
	void reset();
	// FLCO 4~7인 LC만 받음. 새로 완성됐으면 true (text()로 꺼냄)
	bool feed(const uint8_t lc9[9]);
	const std::string &text() const { return text_; }
	static bool is_talker_alias(const uint8_t lc9[9]) { const uint8_t f = lc9[0] & 0x3F; return f >= 4 && f <= 7; }
private:
	std::array<std::array<uint8_t, 9>, 4> pdu_ = {};
	unsigned have_ = 0;
	std::string text_;
	bool done_ = false;
};

} // namespace dmr
