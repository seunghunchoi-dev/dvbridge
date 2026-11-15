// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// HomebrewClient + VoiceTx/VoiceRx 시험
//   모드 local : 가짜 마스터(127.0.0.1)와 로그인 → 음성 2초 송신 → 에코 수신 → 해제, 틀린 비밀번호 → 재시도 간격 증가
//   모드 login : 실제 마스터에 **로그인만** (음성 송신 없음) 후 5초 유지하고 RPTCL. 비밀번호는 환경변수 DMRB_PW
// 사용: homebrew_test local <포트>
//       homebrew_test login <호스트> <포트> <핫스팟ID> <콜사인>
#include <cstdio>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include "net/homebrew_client.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

static bool wait_until(const std::function<bool()> &cond, int ms)
{
	QElapsedTimer t; t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		if (cond()) return true;
		QThread::msleep(5);
	}
	return cond();
}
static void pump(int ms) { wait_until([] { return false; }, ms); }

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	qRegisterMetaType<dmr::Dmrd>("dmr::Dmrd");
	const QString mode = argc > 1 ? argv[1] : "local";

	if (mode == "login") {
		if (argc < 6) { std::printf("usage: homebrew_test login <host> <port> <id> <callsign>\n"); return 2; }
		HomebrewConfig c;
		c.host = argv[2]; c.port = quint16(QString(argv[3]).toUInt()); c.id = QString(argv[4]).toUInt(); c.callsign = argv[5];
		c.password = qEnvironmentVariable("DMRB_PW");
		if (c.password.isEmpty()) { std::printf("DMRB_PW 환경변수에 비밀번호\n"); return 2; }
		HomebrewClient h;
		QObject::connect(&h, &HomebrewClient::log, [](QString s) { std::printf("  log: %s\n", s.toUtf8().constData()); });
		std::printf("실제 마스터 로그인 시험 (음성 송신 없음)\n");
		h.start(c);
		const bool ok = wait_until([&] { return h.connected() || h.state() == HomebrewClient::Backoff; }, 15000);
		CHECK(ok && h.connected(), "로그인 성공");
		if (h.connected()) { pump(5000); CHECK(h.connected(), "5초 유지 (핑/퐁)"); }
		h.stop();
		pump(300);
		std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
		return g_fail ? 1 : 0;
	}

	const quint16 port = argc > 2 ? quint16(QString(argv[2]).toUInt()) : 62031;
	HomebrewConfig c;
	c.host = "127.0.0.1"; c.port = port; c.id = 450000199; c.callsign = "HL0EXM"; c.password = "local";
	c.options = "TS2=450";

	std::printf("A) 로그인·RPTC·옵션\n");
	CHECK(HomebrewClient::build_config_packet(c).size() == 302, "RPTC 설정 패킷 302바이트");
	HomebrewClient h;
	int rx_frames = 0, rx_start = 0, rx_end = 0;
	uint32_t rx_src = 0;
	dmr::VoiceRx vrx;
	QObject::connect(&h, &HomebrewClient::dmrd_received, [&](dmr::Dmrd p) {
		std::vector<dmr::VoiceRx::Event> ev;
		vrx.feed(p, ev);
		for (auto &e : ev) {
			if (e.kind == dmr::VoiceRx::Event::Start) { ++rx_start; rx_src = e.lc.src; }
			else if (e.kind == dmr::VoiceRx::Event::Frames) rx_frames += 3;
			else ++rx_end;
		}
	});
	h.start(c);
	CHECK(wait_until([&] { return h.connected(); }, 5000), "가짜 마스터에 접속");

	std::printf("B) 음성 2초 송신 → 에코 수신\n");
	dmr::VoiceTx tx;
	std::vector<dmr::Dmrd> out;
	dmr::FullLc lc; lc.flco = dmr::kFlcoPrivate; lc.src = 4500001; lc.dst = 9990;
	tx.start(lc, c.id, 2, 1, 0xA1B2C3D4, out);
	for (int i = 0; i < 99; ++i) tx.add_frame(dmr::VoiceTx::silence(), out);	// 99프레임 = 33버스트 ≈ 2초
	tx.end(out);
	CHECK(out.size() == 35, "헤더 1 + 음성 33 + 종료 1 = 35패킷");
	for (auto &p : out) { h.send(p); pump(60); }	// 실제처럼 60ms 간격
	CHECK(wait_until([&] { return rx_end >= 1; }, 6000), "에코 스트림 수신 완료");
	// 가짜 마스터는 DMRD 머리의 송신자만 9990으로 바꾸고 버스트 안 LC는 그대로 → LC 기준 송신자는 4500001
	CHECK(rx_start >= 1 && rx_src == 4500001, "수신 시작: 헤더 LC의 송신자 정보 읽음");
	CHECK(rx_frames == 99, "AMBE 99프레임 수신");

	std::printf("C) 해제\n");
	h.stop();
	pump(300);
	CHECK(h.state() == HomebrewClient::Stopped, "정지 상태");

	std::printf("D) 틀린 비밀번호 → 거부 → 재시도 간격 증가\n");
	HomebrewClient h2;
	HomebrewConfig bad = c; bad.password = "wrong";
	int naks = 0;
	QObject::connect(&h2, &HomebrewClient::log, [&](QString s) { if (s.contains("MSTNAK")) ++naks; });
	h2.start(bad);
	CHECK(wait_until([&] { return h2.state() == HomebrewClient::Backoff; }, 5000), "거부되면 대기 상태");
	CHECK(naks == 1 && h2.next_backoff_seconds() == 10, "첫 거부 후 5초 대기, 다음 대기 10초로 증가");
	pump(1500);
	CHECK(naks == 1, "대기 시간 동안 재시도 안 함");
	h2.stop();

	std::printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS\n");
	return g_fail ? 1 : 0;
}
