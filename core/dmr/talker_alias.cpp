// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "talker_alias.h"

namespace dmr {

static bool get_bit(const uint8_t *b, int i) { return (b[i >> 3] >> (7 - (i & 7))) & 1; }
static void set_bit(uint8_t *b, int i, bool v)
{
	if (v) b[i >> 3] |= uint8_t(0x80 >> (i & 7));
	else b[i >> 3] &= uint8_t(~(0x80 >> (i & 7)));
}

// 7비트 형식에서 필요한 LC 수 (5.4.3)
static int pdus_for(int format, int len)
{
	static const int cap[4][4] = {{7, 15, 23, 31}, {6, 13, 20, 27}, {6, 13, 20, 27}, {3, 6, 10, 13}};
	const int f = format & 3;
	for (int k = 0; k < 4; ++k) if (len <= cap[f][k]) return k + 1;
	return 4;
}

std::vector<std::array<uint8_t, 9>> talker_alias_encode(const std::string &text)
{
	std::string s;
	for (unsigned char c : text) {
		if (s.size() >= 31) break;
		if (c >= 0x80) {		// UTF-8 다중 바이트 → '?' 하나
			if ((c & 0xC0) == 0xC0) s += '?';
			continue;
		}
		s += char(c < 0x20 ? ' ' : c);
	}
	const int len = int(s.size());
	std::vector<std::array<uint8_t, 9>> out;
	if (len == 0) return out;
	const int n = pdus_for(0, len);
	// 글자 비트열: 머리 49 + 블록 56×3 = 217비트 = 31×7
	uint8_t bits[28] = {};
	for (int i = 0; i < len; ++i)
		for (int k = 0; k < 7; ++k) set_bit(bits, i * 7 + k, (uint8_t(s[size_t(i)]) >> (6 - k)) & 1);
	int pos = 0;
	for (int p = 0; p < n; ++p) {
		std::array<uint8_t, 9> lc = {};
		lc[0] = uint8_t((p == 0 ? kFlcoTalkerAliasHdr : kFlcoTalkerAliasBlk1 + p - 1) & 0x3F);	// PF=0, R=0
		lc[1] = 0;																				// FID = 표준
		int o = 16;
		if (p == 0) {
			// 형식 00(7비트) 2비트 + 길이 5비트
			for (int k = 0; k < 2; ++k) set_bit(lc.data(), o++, false);
			for (int k = 0; k < 5; ++k) set_bit(lc.data(), o++, (len >> (4 - k)) & 1);
		}
		while (o < 72) set_bit(lc.data(), o++, get_bit(bits, pos++));
		out.push_back(lc);
	}
	return out;
}

void TalkerAliasRx::reset()
{
	have_ = 0;
	done_ = false;
	text_.clear();
}

static void put_utf8(std::string &s, uint32_t cp)
{
	if (cp < 0x80) s += char(cp);
	else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
	else { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
}

bool TalkerAliasRx::feed(const uint8_t lc9[9])
{
	if (!is_talker_alias(lc9) || lc9[1] != 0) return false;		// 표준 FID만
	const int idx = (lc9[0] & 0x3F) - kFlcoTalkerAliasHdr;
	bool same = (have_ >> idx) & 1;
	for (int i = 0; same && i < 9; ++i) same = pdu_[size_t(idx)][size_t(i)] == lc9[i];
	if (same) return false;										// 이미 받은 것 (같은 이름표가 반복됨)
	if (idx == 0 && ((have_ & 1) != 0)) { have_ = 0; done_ = false; }	// 머리가 바뀜 → 새 이름표
	for (int i = 0; i < 9; ++i) pdu_[size_t(idx)][size_t(i)] = lc9[i];
	have_ |= 1u << idx;
	if (!(have_ & 1)) return false;
	const uint8_t *h = pdu_[0].data();
	const int format = (h[2] >> 6) & 3;
	const int len = (h[2] >> 1) & 0x1F;
	const int n = pdus_for(format, len);
	if ((have_ & ((1u << n) - 1)) != ((1u << n) - 1)) return false;
	// 자료 비트 모으기: 머리 49비트(8·16비트 형식은 첫 비트 예약) + 블록 56비트씩
	uint8_t bits[28] = {};
	int nb = 0;
	for (int i = (format == 0 ? 23 : 24); i < 72; ++i) set_bit(bits, nb++, get_bit(h, i));
	for (int p = 1; p < n; ++p)
		for (int i = 16; i < 72; ++i) set_bit(bits, nb++, get_bit(pdu_[size_t(p)].data(), i));
	std::string s;
	if (format == 0) {
		for (int c = 0; c < len && (c + 1) * 7 <= nb; ++c) {
			uint8_t v = 0;
			for (int k = 0; k < 7; ++k) v = uint8_t((v << 1) | get_bit(bits, c * 7 + k));
			s += char(v);
		}
	}
	else if (format == 3) {
		for (int c = 0; c < len && (c + 1) * 16 <= nb; ++c) {
			uint32_t v = 0;
			for (int k = 0; k < 16; ++k) v = (v << 1) | uint32_t(get_bit(bits, c * 16 + k));
			if (v < 0xD800 || v > 0xDFFF) put_utf8(s, v);
		}
	}
	else {
		for (int c = 0; c < len && (c + 1) * 8 <= nb; ++c) {
			uint8_t v = 0;
			for (int k = 0; k < 8; ++k) v = uint8_t((v << 1) | get_bit(bits, c * 8 + k));
			if (format == 1) put_utf8(s, v);	// ISO 8859-1 → UTF-8
			else s += char(v);					// 이미 UTF-8
		}
	}
	// 끝의 공백·NUL 정리
	while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
	if (done_ && s == text_) return false;
	text_ = s;
	done_ = true;
	return !s.empty();
}

} // namespace dmr
