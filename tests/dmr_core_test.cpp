// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// DMR 코어 시험
//   1) 표준 자체 검증: 표준에 적힌 값(RS 생성다항식·생성행렬, 최소거리)과 일치하는지
//   2) 왕복·오류정정: 부호화 → 비트 오류 주입 → 복호
//   3) 블랙박스 비교: 다른 프로그램이 실제로 보낸 DMRD 패킷(tests/oracle/*.hex)과 바이트 단위 비교
//      (그 프로그램의 소스는 보지 않고 출력 바이트만 사용)
// 사용: dmr_core_test <oracle 폴더>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "dmr/fec.h"
#include "dmr/burst.h"
#include "dmr/voice.h"

using namespace dmr;
static int g_fail = 0, g_ok = 0;
#define CHECK(c, msg) do { if (c) { ++g_ok; } else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

static int popc(uint32_t v) { int c = 0; while (v) { v &= v - 1; ++c; } return c; }
static int min_distance(const LinearCode &c)
{
	int best = 99;
	for (uint32_t m = 1; m < (1u << c.k()); ++m) best = std::min(best, popc(c.encode(m)));
	return best;
}

static std::vector<std::vector<uint8_t>> load_hex(const std::string &path)
{
	std::vector<std::vector<uint8_t>> out;
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line)) {
		std::vector<uint8_t> b;
		for (size_t i = 0; i + 1 < line.size(); i += 2) b.push_back(uint8_t(std::stoi(line.substr(i, 2), nullptr, 16)));
		if (b.size() == 55) out.push_back(b);
	}
	return out;
}

static void oracle(const std::string &path, uint32_t src, uint32_t dst, bool priv, const char *name)
{
	auto pk = load_hex(path);
	std::printf("[%s] 비교 자료 %zu개 패킷\n", name, pk.size());
	CHECK(pk.size() >= 8, "비교 자료 로드");
	if (pk.size() < 8) return;
	FullLc want;
	want.flco = priv ? kFlcoPrivate : kFlcoGroup;
	if (src == 0) { Dmrd f0; Dmrd::unpack(pk[0].data(), int(pk[0].size()), f0); src = f0.src; }	// 0 = 비교 자료의 송신자
	want.src = src;
	want.dst = dst;
	int hdr = 0, term = 0, voice_ok = 0, voice_n = 0, lc_from_embedded = 0;
	uint8_t emb_cc = 0;	// 음성 버스트 EMB의 색상코드 (자료에서 읽음)
	uint64_t vsync = kSyncMsVoice;
	uint32_t frags[4] = {};
	int got_frag = 0;
	for (auto &p : pk) {
		Burst b;
		std::memcpy(b.data(), p.data() + 20, 33);
		const uint8_t fl = p[15];
		const int ftype = (fl >> 4) & 3, dtype = fl & 0xF;
		if (ftype == 1) vsync = burst_center(b);
		if (ftype == 0) {
			const uint64_t c = burst_center(b);
			uint8_t ecc; bool pi; Lcss l;
			if (emb_decode(uint16_t(((c >> 40) << 8) | (c & 0xFF)), ecc, pi, l)) emb_cc = ecc;
		}
		if (ftype == 2) {	// 데이터 동기 (헤더/종료)
			FullLc lc; DataType dt; uint8_t rcc;
			const bool ok = parse_lc_burst(b, lc, dt, rcc);
			CHECK(ok, "헤더/종료 버스트 해석");
			if (!ok) continue;
			CHECK(lc.src == src && lc.dst == dst && lc.is_private() == priv, "LC 송신자·대상·호출종류");
			CHECK(int(dt) == dtype, "슬롯타입 데이터타입 = DMRD 플래그");
			const Burst mine = build_lc_burst(lc, dt, rcc);	// 기본 = MS 데이터 동기
			if (burst_center(b) != kSyncMsData) std::printf("  자료의 데이터 동기 = %012llx\n", (unsigned long long)burst_center(b));
			const bool same = (mine == b);
			CHECK(same, "우리가 만든 헤더/종료 버스트가 33바이트 모두 일치");
			if (same) { if (dt == DT_VOICE_LC_HEADER) ++hdr; else ++term; }
			want.service = lc.service;	// 서비스 옵션 값은 자료에서 확인
		}
		else {	// 음성 (ftype 1 = A(동기), 0 = B~F)
			const int n = (ftype == 1) ? 0 : dtype;	// A=0, B..F = 1..5
			AmbeFrame f[3];
			extract_ambe(b, f);
			uint32_t mine_frag[4];
			embedded_lc_encode(want, mine_frag);
			const Burst mine = build_voice_burst(f, n, emb_cc, mine_frag, vsync);
			++voice_n;
			if (mine == b) ++voice_ok;
			else std::printf("  음성 %c 불일치: 자료 가운데=%012llx 우리=%012llx\n", 'A' + n,
				(unsigned long long)burst_center(b), (unsigned long long)burst_center(mine));
			if (n >= 1 && n <= 4) {
				const uint64_t c = burst_center(b);
				frags[n - 1] = uint32_t((c >> 8) & 0xFFFFFFFFu);
				got_frag |= 1 << (n - 1);
				if (got_frag == 0xF) {
					FullLc lc;
					if (embedded_lc_decode(frags, lc) && lc.src == src && lc.dst == dst) ++lc_from_embedded;
					got_frag = 0;
				}
			}
		}
	}
	std::printf("  자료: 음성 동기 %012llx (%s), EMB 색상코드 %d\n", (unsigned long long)vsync, vsync == kSyncMsVoice ? "MS" : "기타", emb_cc);
	CHECK(vsync == kSyncMsVoice, "자료의 음성 동기 = MS 음성 동기 (우리 기본값)");
	std::printf("  헤더 일치 %d, 종료 일치 %d, 음성 버스트 일치 %d/%d, 임베디드 LC 해석 %d회\n", hdr, term, voice_ok, voice_n, lc_from_embedded);
	CHECK(hdr >= 1 && term >= 1, "헤더·종료 각 1개 이상 일치");
	CHECK(voice_n > 0 && voice_ok == voice_n, "음성 버스트 전부 일치 (동기·EMB·임베디드 LC·음성 비트 배치)");
	CHECK(lc_from_embedded >= 1, "자료의 임베디드 LC 4조각을 우리 복호기로 해석");
}

// VoiceTx에 자료의 AMBE 프레임을 넣었을 때 DMRD 패킷이 자료와 같은지 (스트림 ID만 자료 값을 그대로 씀)
static void oracle_voicetx(const std::string &path, const char *name)
{
	auto pk = load_hex(path);
	if (pk.size() < 3) { CHECK(false, "자료 없음"); return; }
	Dmrd first; Dmrd::unpack(pk[0].data(), int(pk[0].size()), first);
	FullLc lc; DataType dt; uint8_t cc = 0;
	CHECK(parse_lc_burst(first.burst, lc, dt, cc), "첫 패킷(헤더) 해석");
	VoiceTx tx;
	std::vector<Dmrd> out;
	tx.start(lc, first.rptr, first.slot, cc, first.stream, out);
	for (size_t i = 1; i + 1 < pk.size(); ++i) {
		Dmrd p; Dmrd::unpack(pk[i].data(), int(pk[i].size()), p);
		AmbeFrame f[3]; extract_ambe(p.burst, f);
		for (auto &x : f) tx.add_frame(x, out);
	}
	tx.end(out);
	int same = 0;
	for (size_t i = 0; i < out.size() && i < pk.size(); ++i) {
		std::vector<uint8_t> a = out[i].pack();
		if (a == pk[i]) ++same;
		else {
			std::printf("  패킷 %zu 다름:\n    자료 ", i);
			for (int k = 0; k < 20; ++k) std::printf("%02x", pk[i][k]);
			std::printf("\n    우리 ");
			for (int k = 0; k < 20; ++k) std::printf("%02x", a[k]);
			std::printf("\n");
		}
	}
	std::printf("[%s] VoiceTx 출력 %zu개 중 자료와 55바이트 완전 일치 %d개 (자료 %zu개)\n", name, out.size(), same, pk.size());
	CHECK(out.size() == pk.size() && same == int(pk.size()), "VoiceTx 출력 = 자료 DMRD 패킷 (순번·플래그·주소·버스트 전부)");

	VoiceRx rx;
	int starts = 0, frames = 0, ends = 0;
	for (auto &raw : pk) {
		Dmrd p; Dmrd::unpack(raw.data(), int(raw.size()), p);
		std::vector<VoiceRx::Event> ev; rx.feed(p, ev);
		for (auto &e : ev) {
			if (e.kind == VoiceRx::Event::Start) { ++starts; CHECK(e.lc_valid && e.lc.src == lc.src && e.lc.dst == lc.dst, "VoiceRx 시작 정보 = 헤더 LC"); }
			else if (e.kind == VoiceRx::Event::Frames) frames += 3;
			else ++ends;
		}
	}
	CHECK(starts == 1 && ends == 1 && frames == int(pk.size() - 2) * 3, "VoiceRx: 시작 1, 종료 1, AMBE 프레임 수 일치");
}


// Talker Alias (TS 102 361-2 5.4.3, 7.1.1.4/5, 7.2.18/19)
static void talker_alias_tests()
{
	std::printf("5) 이름표(Talker Alias)\n");
	// 길이별 LC 수 (7비트: 7 / 15 / 23 / 31자 경계)
	CHECK(talker_alias_encode("ABCDEFG").size() == 1, "TA 7자 = 머리 1개");
	CHECK(talker_alias_encode("ABCDEFGH").size() == 2, "TA 8자 = 머리+블록1");
	CHECK(talker_alias_encode(std::string(23, 'X')).size() == 3, "TA 23자 = 3개");
	CHECK(talker_alias_encode(std::string(40, 'X')).size() == 4, "TA 31자 넘으면 4개로 자름");
	const std::string t = "HL0ABC DMRBridge Galaxy A17";
	auto pdus = talker_alias_encode(t);
	CHECK(pdus.size() == 4, "TA 27자 = 4개");
	CHECK((pdus[0][0] & 0x3F) == 4 && (pdus[1][0] & 0x3F) == 5 && (pdus[3][0] & 0x3F) == 7 && pdus[0][1] == 0, "TA FLCO 4~7, FID 0");
	CHECK(((pdus[0][2] >> 6) & 3) == 0 && ((pdus[0][2] >> 1) & 0x1F) == 27, "TA 머리: 형식 7비트, 길이 27");
	// 첫 글자 'H'(1001000)는 머리 23번째 비트부터
	CHECK((pdus[0][2] & 1) == 1 && (pdus[0][3] >> 2) == 0x08, "TA 머리 첫 글자 비트 위치");
	TalkerAliasRx rx;
	bool done = false;
	for (int i = 3; i >= 0; --i) done = rx.feed(pdus[size_t(i)].data());	// 순서가 섞여도
	CHECK(done && rx.text() == t, "TA 왕복 (거꾸로 받아도)");
	CHECK(!rx.feed(pdus[0].data()), "TA 같은 머리 반복은 새 완성 아님");
	// 8비트(ISO) 형식 직접 만들어 해석: 머리 자료 첫 비트 예약, 6자씩
	{
		std::array<uint8_t, 9> h = {4, 0, uint8_t((1 << 6) | (5 << 1)), 'K', 'o', 'r', 'e', 'a', 0};
		TalkerAliasRx r8;
		CHECK(r8.feed(h.data()) && r8.text() == "Korea", "TA ISO 8비트 형식 해석");
	}
	// 음성 송수신 전체: VoiceTx 이름표 → VoiceRx Alias 이벤트, 늦은 진입에서도 통화 정보는 음성 LC로
	FullLc lc; lc.flco = kFlcoGroup; lc.src = 4500001; lc.dst = 450;
	VoiceTx tx; tx.set_talker_alias(t);
	std::vector<Dmrd> out;
	tx.start(lc, 450000101, 2, 1, 0x1234, out);
	for (int i = 0; i < 3 * 6 * 12; ++i) tx.add_frame(VoiceTx::silence(), out);	// 12 슈퍼프레임
	tx.end(out);
	VoiceRx vr;
	std::vector<VoiceRx::Event> ev;
	for (const Dmrd &d : out) vr.feed(d, ev);
	std::string got;
	int aliases = 0;
	for (auto &e : ev) if (e.kind == VoiceRx::Event::Alias) { got = e.alias; ++aliases; }
	CHECK(got == t && aliases == 1, "음성 안 이름표 수신 (한 번만 알림)");
	// 늦은 진입: 헤더 없이 이름표 슈퍼프레임부터 받기 시작
	VoiceRx late;
	std::vector<VoiceRx::Event> ev2;
	for (size_t i = 1 + 6; i < out.size(); ++i) late.feed(out[i], ev2);	// 첫 슈퍼프레임(음성 LC) 건너뜀
	bool lc_ok = false, bad = false;
	for (auto &e : ev2) if (e.kind == VoiceRx::Event::Start && e.lc_valid) { lc_ok = e.lc.src == 4500001 && e.lc.dst == 450; bad |= !lc_ok; }
	CHECK(lc_ok && !bad, "늦은 진입: 이름표 LC를 통화 정보로 착각하지 않음");
	// 이름표 없으면 예전과 같은 출력 (블랙박스 일치 유지는 4)에서 확인)
}

int main(int argc, char **argv)
{
	std::printf("1) 표준 값 검증\n");
	CHECK(min_distance(golay2087()) >= 7, "Golay(20,8) 최소거리 7 이상 (표준 표기 (20,8,7))");
	CHECK(min_distance(qr1676()) == 6, "QR(16,7) 최소거리 6");
	CHECK(min_distance(hamming1393()) == 3, "Hamming(13,9) 최소거리 3");
	CHECK(min_distance(hamming15113()) == 3, "Hamming(15,11) 최소거리 3");
	CHECK(min_distance(hamming16114()) == 4, "Hamming(16,11) 최소거리 4");
	{	// RS(12,9) 생성행렬 표 B.18: 단위 메시지의 패리티 = 표의 마지막 3열
		static const uint8_t tb18[9][3] = {{0x1C,0xBC,0xFD},{0x89,0x31,0x08},{0xAD,0x41,0x36},{0x7D,0x71,0x16},{0xF3,0xA6,0x3A},
			{0x08,0x83,0x7B},{0x3F,0x6F,0x02},{0x6C,0x0D,0xA7},{0x0E,0x38,0x40}};
		bool all = true;
		for (int r = 0; r < 9; ++r) {
			uint8_t m[9] = {}; m[r] = 1;
			uint8_t p[3]; rs129_parity(m, p);
			if (p[0] != tb18[r][0] || p[1] != tb18[r][1] || p[2] != tb18[r][2]) {
				all = false;
				std::printf("  RS 행 %d: %02X %02X %02X (표준 %02X %02X %02X)\n", r, p[0], p[1], p[2], tb18[r][0], tb18[r][1], tb18[r][2]);
			}
		}
		CHECK(all, "RS(12,9) 패리티 = 표준 생성행렬(표 B.18)");
	}
	CHECK(gf_mul(0x02, 0x80) == 0x1D, "GF(2^8): α^8 = 1D (표 B.19)");

	std::printf("2) 왕복·오류정정\n");
	std::srand(12345);
	int bptc_ok = 0, emb_ok = 0, elc_ok = 0, lcb_ok = 0;
	for (int t = 0; t < 200; ++t) {
		uint8_t in[12]; for (auto &x : in) x = uint8_t(std::rand());
		bool bits[196]; bptc19696_encode(in, bits);
		const int e = t % 3;	// 0~2비트 오류 (서로 다른 행·열이 아닐 수 있어 1비트만 보장 기준으로 판단)
		for (int i = 0; i < e && i < 1; ++i) bits[std::rand() % 196] ^= 1;
		uint8_t out[12];
		if (bptc19696_decode(bits, out) >= 0 && !std::memcmp(in, out, 12)) ++bptc_ok;

		const uint8_t cc = uint8_t(std::rand() & 0xF);
		const Lcss l = Lcss(std::rand() & 3);
		uint16_t emb = emb_encode(cc, false, l);
		emb ^= uint16_t(1u << (std::rand() % 16)) ^ uint16_t(1u << (std::rand() % 16));
		uint8_t rcc; bool pi; Lcss rl;
		if (emb_decode(emb, rcc, pi, rl) && rcc == cc && rl == l) ++emb_ok;

		FullLc lc; lc.flco = (t & 1) ? 3 : 0; lc.src = uint32_t(std::rand()) & 0xFFFFFF; lc.dst = uint32_t(std::rand()) & 0xFFFFFF;
		uint32_t fr[4]; embedded_lc_encode(lc, fr);
		fr[std::rand() % 4] ^= 1u << (std::rand() % 32);
		FullLc lc2;
		if (embedded_lc_decode(fr, lc2) && lc2.src == lc.src && lc2.dst == lc.dst && lc2.flco == lc.flco) ++elc_ok;

		Burst b = build_lc_burst(lc, (t & 2) ? DT_TERMINATOR_WITH_LC : DT_VOICE_LC_HEADER, cc);
		b[std::rand() % 12] ^= uint8_t(1u << (std::rand() % 8));	// 정보 영역 1비트 오류
		FullLc lc3; DataType dt; uint8_t c3;
		if (parse_lc_burst(b, lc3, dt, c3) && lc3.src == lc.src && lc3.dst == lc.dst && c3 == cc) ++lcb_ok;
	}
	std::printf("  BPTC %d/200, EMB(2비트 오류) %d/200, 임베디드 LC(1비트 오류) %d/200, 헤더 버스트(1비트 오류) %d/200\n", bptc_ok, emb_ok, elc_ok, lcb_ok);
	CHECK(bptc_ok == 200, "BPTC(196,96) 왕복·1비트 정정");
	CHECK(emb_ok == 200, "EMB QR(16,7) 2비트 정정");
	CHECK(elc_ok == 200, "임베디드 LC 왕복·1비트 정정");
	CHECK(lcb_ok == 200, "헤더/종료 버스트 왕복·1비트 정정");
	{
		AmbeFrame f[3]; for (int k = 0; k < 3; ++k) for (int i = 0; i < 9; ++i) f[k][i] = uint8_t(k * 40 + i * 7 + 1);
		uint32_t fr[4] = {};
		Burst b = build_voice_burst(f, 2, 1, fr);
		AmbeFrame g[3]; extract_ambe(b, g);
		CHECK(f[0] == g[0] && f[1] == g[1] && f[2] == g[2], "음성 버스트 AMBE 3프레임 왕복");
	}

	if (argc >= 2) {
		std::printf("3) 블랙박스 비교\n");
		const std::string dir = argv[1];
		oracle(dir + "/dmrd_private.hex", 0, 9990, true, "개인호출 →9990");
		oracle(dir + "/dmrd_group.hex", 0, 450, false, "그룹호출 →450");
		std::printf("4) 음성 순서기(VoiceTx/VoiceRx) 블랙박스 비교\n");
		oracle_voicetx(dir + "/dmrd_private.hex", "개인호출");
		oracle_voicetx(dir + "/dmrd_group.hex", "그룹호출");
	}

	talker_alias_tests();
	std::printf("\n%d개 확인, 실패 %d\nRESULT: %s\n", g_ok + g_fail, g_fail, g_fail ? "FAIL" : "PASS");
	return g_fail ? 1 : 0;
}
