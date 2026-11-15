// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// DMRD 패킷(Homebrew 프로토콜, 55바이트)과 음성 송수신 순서기
//   DMRD: "DMRD" 순번1 송신자3 대상3 중계기4 플래그1 스트림ID4 버스트33 BER1 RSSI1
//   플래그: bit7 슬롯(1=TS2) bit6 호출종류(1=개인) bit5-4 프레임종류(0=음성 1=음성동기 2=데이터동기)
//           bit3-0 데이터타입(데이터동기일 때) 또는 음성 순번(A=0 … F=5)
#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include "burst.h"
#include "talker_alias.h"

namespace dmr {

struct Dmrd {
	uint8_t seq = 0;
	uint32_t src = 0, dst = 0, rptr = 0;
	uint8_t slot = 2;			// 1 또는 2
	bool priv = false;
	uint8_t ftype = 0;			// 0 음성, 1 음성동기(A), 2 데이터동기
	uint8_t dtype_vseq = 0;
	uint32_t stream = 0;
	Burst burst{};
	uint8_t ber = 0, rssi = 0;

	std::vector<uint8_t> pack() const;
	static bool unpack(const uint8_t *d, int len, Dmrd &out);
	bool is_header() const { return ftype == 2 && dtype_vseq == DT_VOICE_LC_HEADER; }
	bool is_terminator() const { return ftype == 2 && dtype_vseq == DT_TERMINATOR_WITH_LC; }
	bool is_voice() const { return ftype == 0 || ftype == 1; }
};

// ---- 송신: 헤더 1개 → 음성 버스트(AMBE 3프레임마다 1개, A~F 반복) → 종료 ----
class VoiceTx {
public:
	void start(const FullLc &lc, uint32_t rptr, uint8_t slot, uint8_t cc, uint32_t stream_id, std::vector<Dmrd> &out);
	void add_frame(const AmbeFrame &f, std::vector<Dmrd> &out);	// 3개 모이면 음성 패킷 1개
	void end(std::vector<Dmrd> &out);							// 남은 프레임은 무음으로 채워 내보낸 뒤 종료
	bool active() const { return active_; }
	// 이름표(Talker Alias). start 전에 지정, 빈 문자열이면 보내지 않음.
	// 내장 LC 순서: 첫 슈퍼프레임 음성 LC → 이름표 머리 → 블록… → 음성 LC → … (늦게 들어온 수신기도 음성 LC를 받게)
	void set_talker_alias(const std::string &text) { ta_ = talker_alias_encode(text); }
	// 무음 AMBE+2 프레임 (FEC 포함 72비트) — 보코더가 없을 때 채움용
	static AmbeFrame silence();
private:
	Dmrd base(uint8_t ftype, uint8_t dv) const;
	void emit_voice(std::vector<Dmrd> &out);
	FullLc lc_;
	uint32_t rptr_ = 0, stream_ = 0;
	uint8_t slot_ = 2, cc_ = 0, seq_ = 0;
	int vseq_ = 0;				// 0=A … 5=F
	uint32_t frag_[4] = {};
	AmbeFrame pending_[3];
	int npending_ = 0;
	bool active_ = false;
	std::vector<std::array<uint8_t, 9>> ta_;
	int superframe_ = 0;
};

// ---- 수신: 스트림 추적 + AMBE 프레임 추출 ----
class VoiceRx {
public:
	struct Event {
		enum Kind { Start, Frames, End, Alias } kind;	// Alias: 이름표(Talker Alias) 완성 → alias
		FullLc lc;					// Start: 헤더 또는 임베디드 LC에서 얻은 정보
		bool lc_valid = false;
		uint32_t stream = 0;
		uint8_t slot = 0;
		AmbeFrame frames[3];		// Frames
		std::string alias;			// Alias (UTF-8)
	};
	// 패킷 하나 처리 → 이벤트 0개 이상
	void feed(const Dmrd &p, std::vector<Event> &out);
	// 일정 시간 패킷이 없으면 호출 → 진행 중 스트림 강제 종료
	void timeout(std::vector<Event> &out);
	bool active() const { return active_; }
	uint32_t stream() const { return stream_; }
private:
	bool active_ = false;
	uint32_t stream_ = 0;
	uint8_t slot_ = 0;
	uint32_t frag_[4] = {};
	int got_frag_ = 0;
	bool lc_sent_ = false;
	TalkerAliasRx ta_;
};

} // namespace dmr
