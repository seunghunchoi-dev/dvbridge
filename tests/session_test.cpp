// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// Session(브릿지) 시험 — PC 내부만 사용
//   이 프로그램이 USRP 클라이언트 역할(QUdpSocket), 가짜 마스터는 tests/tools/fake_master.py (QProcess로 실행)
// 사용: session_test <fake_master.py 경로> <보코더 plugins 폴더>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkDatagram>
#include <QProcess>
#include <QThread>
#include <QUdpSocket>
#include <QtEndian>
#include "session/session.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

static bool wait_until(const std::function<bool()> &cond, int ms)
{
	QElapsedTimer t; t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		if (cond()) return true;
		QThread::msleep(2);
	}
	return cond();
}
static void pump(int ms) { wait_until([] { return false; }, ms); }

// ---- USRP 클라이언트 (DVSwitch Mobile 흉내: type 칸 리틀엔디안, 352바이트) ----
struct Client {
	QUdpSocket s;
	quint16 srv = 0;
	quint32 seq = 0;
	int voice = 0, unkey = 0, peak = 0, reg_ok = 0;
	QList<QByteArray> set_info;
	QString last_info;
	void send(quint32 type, bool keyup, const QByteArray &payload)
	{
		QByteArray d(352, '\0');
		uchar *h = reinterpret_cast<uchar *>(d.data());
		std::memcpy(h, "USRP", 4);
		qToBigEndian<quint32>(seq++, h + 4);
		qToBigEndian<quint32>(keyup ? 1 : 0, h + 12);
		qToLittleEndian<quint32>(type, h + 20);
		std::memcpy(h + 32, payload.constData(), qMin(int(payload.size()), 320));
		s.writeDatagram(d, QHostAddress::LocalHost, srv);
	}
	void drain()
	{
		while (s.hasPendingDatagrams()) {
			const QByteArray d = s.receiveDatagram().data();
			if (d.size() < 32 || std::memcmp(d.constData(), "USRP", 4)) continue;
			const uchar *h = reinterpret_cast<const uchar *>(d.constData());
			const quint32 keyup = qFromBigEndian<quint32>(h + 12);
			const quint32 type = qFromLittleEndian<quint32>(h + 20);	// 우리가 보낸 순서(LE)로 응답해야 함
			if (type == 0) {
				if (keyup && d.size() >= 352) {
					++voice;
					for (int i = 0; i < 160; ++i) peak = qMax(peak, qAbs(int(qFromLittleEndian<qint16>(h + 32 + i * 2))));
				} else if (!keyup) ++unkey;
			} else if (type == 2) {
				const QByteArray pl = d.mid(32);
				if (pl.startsWith("REG:OK")) ++reg_ok;
				else if (pl.startsWith("INFO:")) last_info = QString::fromLatin1(pl.left(pl.indexOf('\0')));
				else if (!pl.isEmpty() && uchar(pl[0]) == 8) set_info.append(pl);
			}
		}
	}
	// keyed 동안 1kHz 톤을 20ms마다 보냄
	void talk(int ms)
	{
		double ph = 0;
		QElapsedTimer t; t.start();
		qint64 next = 0;
		while (t.elapsed() < ms) {
			if (t.elapsed() >= next) {
				QByteArray a(320, '\0');
				for (int i = 0; i < 160; ++i) { qToLittleEndian<qint16>(qint16(8000 * std::sin(ph)), reinterpret_cast<uchar *>(a.data()) + i * 2); ph += 2 * M_PI * 1000 / 8000; }
				send(0, true, a);
				next += 20;
			}
			QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
			drain();
			QThread::msleep(1);
		}
		send(0, false, QByteArray());
	}
};

static int count_streams(const QString &logpath, const QString &needle)
{
	QFile f(logpath);
	if (!f.open(QIODevice::ReadOnly)) return -1;
	return QString::fromUtf8(f.readAll()).count(needle);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	qRegisterMetaType<dmr::Dmrd>("dmr::Dmrd");
	if (argc < 3) { std::printf("usage: session_test <fake_master.py> <plugins dir>\n"); return 2; }
	const QString master_py = QString::fromLocal8Bit(argv[1]);
	const QString plugins = QString::fromLocal8Bit(argv[2]);
	const QString mlog = QDir::tempPath() + "/dvbridge_session_master.log";
	QFile::remove(mlog);

	QProcess master;
	master.start("python", {"-I", master_py, "60", "62071", "local", mlog});
	CHECK(master.waitForStarted(5000), "가짜 마스터 실행");
	pump(800);

	SessionConfig c;
	c.name = "HL0EXM";
	c.master.host = "127.0.0.1"; c.master.port = 62071; c.master.id = 450000199; c.master.callsign = "HL0EXM"; c.master.password = "local";
	c.dmr_id = 4500001; c.tg = 9990; c.priv = true; c.usrp_port = 32071; c.plugin_dir = plugins;
	Session s(c);
	QObject::connect(&s, &Session::log, [](QString l) { std::printf("    · %s\n", l.toUtf8().constData()); });
	QList<CallRecord> calls;
	QObject::connect(&s, &Session::call_finished, [&](CallRecord r) { calls.append(r); });
	CHECK(s.start(), "세션 시작 (USRP 포트·보코더)");
	CHECK(wait_until([&] { return s.master()->connected(); }, 5000), "마스터 접속");

	Client cl;
	cl.s.bind(QHostAddress::LocalHost, 0);
	cl.srv = 32071;

	std::printf("A) 등록·TG 변경\n");
	cl.send(2, false, QByteArray("REG:DVSWITCH", 13));
	wait_until([&] { cl.drain(); return cl.reg_ok > 0; }, 1500);
	CHECK(cl.reg_ok == 1, "REG:OK 응답 (리틀엔디안 type)");
	cl.send(1, false, QByteArray("450", 4));
	wait_until([&] { cl.drain(); return cl.last_info.contains("\"last_tune\":\"450\""); }, 1500);
	CHECK(s.config().tg == 450 && !s.config().priv, "DTMF 450 → TG 450 그룹");
	cl.send(1, false, QByteArray("9990", 5));
	pump(300);
	CHECK(s.config().tg == 9990 && s.config().priv, "DTMF 9990('#' 없음) → 9990 개인호출로 자동");
	cl.send(1, false, QByteArray("450997", 7));
	pump(300);
	CHECK(s.config().tg == 450997 && s.config().priv, "DTMF 450997('#' 없음, 한국 Parrot) → 개인호출로 자동");
	cl.send(1, false, QByteArray("450", 4));
	pump(300);

	std::printf("B) 톤 3초 송신 → 에코\n");
	cl.talk(3000);
	CHECK(wait_until([&] { cl.drain(); return cl.unkey >= 1; }, 8000), "에코 끝(keyup=0) 수신");
	pump(300); cl.drain();
	std::printf("     에코 음성 %d패킷, 최대 레벨 %d, 수신 정보 %d개\n", cl.voice, cl.peak, int(cl.set_info.size()));
	CHECK(cl.voice > 120, "에코 음성 패킷 수신 (3초 ≈ 150)");
	if (QDir(plugins).entryList({"vocoder_*.dll"}, QDir::Files).isEmpty()) std::printf("  [건너뜀] 에코에 실제 소리 - plugins 폴더에 보코더가 없음\n");
	else CHECK(cl.peak > 2000, "에코에 실제 소리 (보코더 왕복)");
	bool info_ok = false;
	for (const QByteArray &si : cl.set_info) {
		const int src = (uchar(si[2]) << 16) | (uchar(si[3]) << 8) | uchar(si[4]);
		const int dst = (uchar(si[9]) << 16) | (uchar(si[10]) << 8) | uchar(si[11]);
		if (src == 4500001 && dst == 450 && si[12] == 2) info_ok = true;
	}
	CHECK(info_ok, "수신 시작 정보(SET_INFO): 4500001 → 450, TS2");
	CHECK(count_streams(mlog, "dst=450 flags=a1") == 1, "마스터가 받은 송신: 대상 450, 그룹, TS2 (헤더 1회)");
	{
		bool tx = false, rx = false;
		for (const CallRecord &r : calls) {
			if (r.tx && r.dst == 450 && !r.priv && r.duration_ms > 2500 && r.src_call == "HL0EXM") tx = true;
			if (!r.tx && r.dst == 450 && r.duration_ms > 2000) rx = true;
		}
		CHECK(tx && rx, "이력: 송신(→450, 약 3초)과 에코 수신이 기록됨");
	}

	std::printf("C) 송신 직후 바로 다시 송신 가능 (임의 간격 제한 없음)\n");
	{
		const int t = s.tx_count();
		cl.talk(2200); pump(100);		// 2초 이상 송신 2번 연속 → 둘 다 송신 (BM 규칙상 세지 않음)
		cl.talk(2200); pump(200);
		CHECK(s.tx_count() == t + 2, "2초 이상 송신은 연달아 해도 막지 않음");
		CHECK(s.short_tx_in_window() == 0, "2초 이상 송신은 짧은 송신으로 세지 않음");
	}

	std::printf("F) Unlink (TG 4000)\n");
	{
		const int before_tx = s.tx_count();
		cl.send(1, false, QByteArray("91", 3));	// 450으로 송신한 적 있음 → 자동 Unlink 후 변경
		pump(200);
		CHECK(s.unlinking(), "TG 변경 시 자동 Unlink 송신 시작");
		cl.talk(300);	// Unlink 중 PTT → 무시
		CHECK(s.tx_count() == before_tx, "Unlink 송신 중 PTT 무시");
		CHECK(wait_until([&] { return !s.unlinking(); }, 5000), "Unlink 송신 끝 (무음 약 2.2초)");
		CHECK(s.unlink_count() == 1 && s.config().tg == 91, "Unlink 1회, 대상 91로 변경");
		CHECK(s.short_tx_in_window() == 0, "Unlink(무음 2.2초)는 짧은 송신으로 세지 않음");
		pump(300);
		cl.send(1, false, QByteArray("4000", 5));	// 수동 Unlink
		pump(200);
		CHECK(wait_until([&] { return !s.unlinking(); }, 5000) && s.unlink_count() == 2, "4000 입력 → 수동 Unlink");
		CHECK(s.config().tg == 91, "수동 Unlink는 통화 대상을 바꾸지 않음");
		cl.send(1, false, QByteArray("9990", 5));	// 91로는 송신 안 했으니 Unlink 없음
		pump(300);
		CHECK(s.unlink_count() == 2, "송신한 적 없는 TG에서 바꿀 때는 Unlink 안 함");
		pump(500);
		CHECK(count_streams(mlog, "dst=4000 flags=a1") == 2, "마스터가 받은 4000 그룹호출 2회");
	}

	std::printf("E) BM 루프 방지 규칙 (60초 안 2초 미만 키업 5회 허용, 6회째 막음)\n");
	{
		SessionConfig c2 = c;
		c2.usrp_port = 32072; c2.master.id = 450000198;
		Session s2(c2);
		CHECK(s2.start(), "두 번째 세션 시작");
		CHECK(wait_until([&] { return s2.master()->connected(); }, 5000), "마스터 접속");
		Client c3; c3.s.bind(QHostAddress::LocalHost, 0); c3.srv = 32072;
		for (int i = 0; i < 6; ++i) { c3.talk(300); pump(300); }
		CHECK(s2.tx_count() == 5, "짧은 송신 5회는 송신, 6번째(BM 차단 조건)는 막음");
		CHECK(s2.short_tx_in_window() == 5, "최근 60초 짧은 송신 5회로 집계");
		s2.stop();
	}

	std::printf("G) 지금 채널만 듣기 + 채널 바꿀 때 자동 Unlink\n");
	{
		SessionConfig c4 = c;
		c4.usrp_port = 32074; c4.dmr_id = 4500003; c4.master.id = 450000397; c4.tg = 450; c4.priv = false;	// 짧은 송신 집계(E)와 다른 ID
		Session s4(c4);
		QStringList logs;
		QObject::connect(&s4, &Session::log, [&](QString l) { logs << l; });
		CHECK(s4.start(), "세 번째 세션 시작");
		CHECK(wait_until([&] { return s4.master()->connected(); }, 5000), "마스터 접속");
		Client c5; c5.s.bind(QHostAddress::LocalHost, 0); c5.srv = 32074;
		c5.talk(2500);		// TG 450 그룹 송신 → 가짜 마스터가 1초 뒤 같은 450 그룹으로 되돌려 보냄
		CHECK(wait_until([&] { return s4.tx_count() == 1; }, 3000), "TG 450 그룹 송신");
		bool txing = true;
		QObject::connect(&s4, &Session::tx_state, [&](bool on, int, bool) { txing = on; });
		wait_until([&] { return !txing; }, 3000);	// 송신이 끝난 직후 (되돌림은 1초 뒤에 옴)
		c5.drain(); c5.voice = 0;
		CHECK(s4.set_target(9990, true, "시험").isEmpty(), "9990 개인호출로 바꿈");
		CHECK(s4.unlinking() || s4.unlink_count() == 1, "450에서 바꿀 때 자동 Unlink");
		pump(4500); c5.drain();
		CHECK(c5.voice == 0, "되돌아온 TG 450 그룹 소리는 휴대폰으로 안 보냄 (지금 채널 9990)");
		CHECK(logs.join("\n").contains(QString::fromUtf8("수신 거름: TG 450")), "기록: 수신 거름 TG 450");
		CHECK(wait_until([&] { return !s4.unlinking(); }, 5000) && s4.unlink_count() == 1, "자동 Unlink 한 번만 (4000 되돌림은 구독으로 안 봄)");
		CHECK(s4.set_target(91, false, "시험").isEmpty() && s4.unlink_count() == 1, "그 뒤 91로 바꿀 때는 Unlink 다시 안 함");
		s4.stop();
	}

	std::printf("D) 마스터 미접속이면 송신 안 함\n");
	s.master()->stop();
	pump(2300);
	const int t0 = s.tx_count();
	cl.talk(300);
	pump(200);
	CHECK(s.tx_count() == t0, "미접속 상태 PTT 무시");

	s.stop();
	master.waitForFinished(65000);
	{ const QByteArray e = master.readAllStandardError(); if (!e.isEmpty()) std::printf("fake_master stderr: %s\n", e.constData()); }
	std::printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS\n");
	return g_fail ? 1 : 0;
}
