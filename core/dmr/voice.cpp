// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "voice.h"
#include <cstring>

namespace dmr {

// ===================== DMRD =====================
std::vector<uint8_t> Dmrd::pack() const
{
	std::vector<uint8_t> d(55, 0);
	std::memcpy(d.data(), "DMRD", 4);
	d[4] = seq;
	d[5] = uint8_t(src >> 16); d[6] = uint8_t(src >> 8); d[7] = uint8_t(src);
	d[8] = uint8_t(dst >> 16); d[9] = uint8_t(dst >> 8); d[10] = uint8_t(dst);
	d[11] = uint8_t(rptr >> 24); d[12] = uint8_t(rptr >> 16); d[13] = uint8_t(rptr >> 8); d[14] = uint8_t(rptr);
	d[15] = uint8_t((slot == 2 ? 0x80 : 0) | (priv ? 0x40 : 0) | ((ftype & 3) << 4) | (dtype_vseq & 0xF));
	d[16] = uint8_t(stream >> 24); d[17] = uint8_t(stream >> 16); d[18] = uint8_t(stream >> 8); d[19] = uint8_t(stream);
	std::memcpy(d.data() + 20, burst.data(), 33);
	d[53] = ber;
	d[54] = rssi;
	return d;
}

bool Dmrd::unpack(const uint8_t *d, int len, Dmrd &o)
{
	if (len < 53 || std::memcmp(d, "DMRD", 4) != 0) return false;
	o.seq = d[4];
	o.src = (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 8) | d[7];
	o.dst = (uint32_t(d[8]) << 16) | (uint32_t(d[9]) << 8) | d[10];
	o.rptr = (uint32_t(d[11]) << 24) | (uint32_t(d[12]) << 16) | (uint32_t(d[13]) << 8) | d[14];
	o.slot = (d[15] & 0x80) ? 2 : 1;
	o.priv = (d[15] & 0x40) != 0;
	o.ftype = (d[15] >> 4) & 3;
	o.dtype_vseq = d[15] & 0xF;
	o.stream = (uint32_t(d[16]) << 24) | (uint32_t(d[17]) << 16) | (uint32_t(d[18]) << 8) | d[19];
	std::memcpy(o.burst.data(), d + 20, 33);
	o.ber = len > 53 ? d[53] : 0;
	o.rssi = len > 54 ? d[54] : 0;
	return true;
}

// ===================== 송신 =====================
AmbeFrame VoiceTx::silence()
{
	// AMBE+2 3600x2450(FEC 포함) 무음 프레임으로 널리 쓰이는 값
	return AmbeFrame{0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B};
}

Dmrd VoiceTx::base(uint8_t ftype, uint8_t dv) const
{
	Dmrd p;
	p.seq = seq_;
	p.src = lc_.src;
	p.dst = lc_.dst;
	p.rptr = rptr_;
	p.slot = slot_;
	p.priv = lc_.is_private();
	p.ftype = ftype;
	p.dtype_vseq = dv;
	p.stream = stream_;
	return p;
}

void VoiceTx::start(const FullLc &lc, uint32_t rptr, uint8_t slot, uint8_t cc, uint32_t stream_id, std::vector<Dmrd> &out)
{
	lc_ = lc; rptr_ = rptr; slot_ = slot; cc_ = cc; stream_ = stream_id;
	seq_ = 0; vseq_ = 0; npending_ = 0; active_ = true; superframe_ = 0;
	embedded_lc_encode(lc_, frag_);
	Dmrd h = base(2, DT_VOICE_LC_HEADER);
	h.burst = build_lc_burst(lc_, DT_VOICE_LC_HEADER, cc_);
	out.push_back(h);
	++seq_;
}

void VoiceTx::emit_voice(std::vector<Dmrd> &out)
{
	if (vseq_ == 0) {	// 새 슈퍼프레임: 이번 B~E에 실을 내장 LC 고르기 (음성 LC, 이름표 LC 1~4개 차례로)
		const int k = ta_.empty() ? 0 : superframe_ % int(ta_.size() + 1);
		if (k == 0) embedded_lc_encode(lc_, frag_);
		else embedded_lc_encode_bytes(ta_[size_t(k - 1)].data(), frag_);
		++superframe_;
	}
	Dmrd v = base(vseq_ == 0 ? 1 : 0, uint8_t(vseq_));
	v.burst = build_voice_burst(pending_, vseq_, cc_, frag_);
	out.push_back(v);
	++seq_;
	vseq_ = (vseq_ + 1) % 6;
	npending_ = 0;
}

void VoiceTx::add_frame(const AmbeFrame &f, std::vector<Dmrd> &out)
{
	if (!active_) return;
	pending_[npending_++] = f;
	if (npending_ == 3) emit_voice(out);
}

void VoiceTx::end(std::vector<Dmrd> &out)
{
	if (!active_) return;
	if (npending_ > 0) {
		while (npending_ < 3) pending_[npending_++] = silence();
		emit_voice(out);
	}
	Dmrd t = base(2, DT_TERMINATOR_WITH_LC);
	t.burst = build_lc_burst(lc_, DT_TERMINATOR_WITH_LC, cc_);
	out.push_back(t);
	++seq_;
	active_ = false;
}

// ===================== 수신 =====================
void VoiceRx::feed(const Dmrd &p, std::vector<Event> &out)
{
	// 다른 스트림이 시작되면 이전 것은 끝난 것으로 처리
	if (active_ && p.stream != stream_) {
		Event e{Event::End}; e.stream = stream_; e.slot = slot_;
		out.push_back(e);
		active_ = false;
	}
	if (p.is_terminator()) {
		if (active_) {
			Event e{Event::End}; e.stream = stream_; e.slot = slot_;
			out.push_back(e);
			active_ = false;
		}
		return;
	}
	if (!active_ && (p.is_header() || p.is_voice())) {
		active_ = true; stream_ = p.stream; slot_ = p.slot;
		got_frag_ = 0; lc_sent_ = false; ta_.reset();
		Event e{Event::Start}; e.stream = p.stream; e.slot = p.slot;
		FullLc lc; DataType dt; uint8_t cc;
		if (p.is_header() && parse_lc_burst(p.burst, lc, dt, cc)) { e.lc = lc; e.lc_valid = true; lc_sent_ = true; }
		else { e.lc.src = p.src; e.lc.dst = p.dst; e.lc.flco = p.priv ? kFlcoPrivate : kFlcoGroup; }	// 헤더를 놓쳤으면 DMRD 주소로
		out.push_back(e);
	}
	if (p.is_voice()) {
		Event e{Event::Frames}; e.stream = stream_; e.slot = slot_;
		extract_ambe(p.burst, e.frames);
		out.push_back(e);
		// 내장 LC 4조각(B~E): 늦게 들어온 경우 통화 정보 복원 + 이름표(Talker Alias)
		if (p.ftype == 0 && p.dtype_vseq >= 1 && p.dtype_vseq <= 4) {
			const uint64_t c = burst_center(p.burst);
			frag_[p.dtype_vseq - 1] = uint32_t((c >> 8) & 0xFFFFFFFFu);
			got_frag_ |= 1 << (p.dtype_vseq - 1);
			uint8_t b[9];
			if (got_frag_ == 0xF && embedded_lc_decode_bytes(frag_, b)) {
				const uint8_t flco = b[0] & 0x3F;
				if (!lc_sent_ && (flco == kFlcoGroup || flco == kFlcoPrivate)) {	// 이름표 LC를 통화 정보로 착각하지 않게
					Event s{Event::Start}; s.stream = stream_; s.slot = slot_; s.lc = FullLc::from_bytes(b); s.lc_valid = true;
					out.push_back(s);	// 같은 스트림의 Start 재통보(정보 보완)
					lc_sent_ = true;
				}
				else if (TalkerAliasRx::is_talker_alias(b) && ta_.feed(b)) {
					Event a{Event::Alias}; a.stream = stream_; a.slot = slot_; a.alias = ta_.text();
					out.push_back(a);
				}
			}
			if (p.dtype_vseq == 4) got_frag_ = 0;
		}
	}
}

void VoiceRx::timeout(std::vector<Event> &out)
{
	if (!active_) return;
	Event e{Event::End}; e.stream = stream_; e.slot = slot_;
	out.push_back(e);
	active_ = false;
}

} // namespace dmr
