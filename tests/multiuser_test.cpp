// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// 다중 사용자 시험 — PC 내부만 (가짜 마스터 1개에 세션 2개)
//   A) 두 사용자가 동시에 3초 송신 → 마스터는 두 스트림(서로 다른 송신자·핫스팟 ID)을 받고, 각자 자기 에코만 받음
//   B) 같은 DMR ID(접미사만 다른 핫스팟 2개)의 짧은 송신은 세션을 넘어 합산 → 합쳐 6번째에서 막음
// 사용: multiuser_test <fake_master.py> <보코더 plugins 폴더>
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
	while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); if (cond()) return true; QThread::msleep(2); }
	return cond();
}
static void pump(int ms) { wait_until([] { return false; }, ms); }

struct Client {
	QUdpSocket s;
	quint16 srv = 0;
	int voice = 0, peak = 0;
	QList<int> info_src;
	void send(bool keyup, const QByteArray &pl)
	{
		QByteArray d(352, '\0');
		uchar *h = reinterpret_cast<uchar *>(d.data());
		std::memcpy(h, "USRP", 4);
		qToBigEndian<quint32>(keyup ? 1 : 0, h + 12);
		std::memcpy(h + 32, pl.constData(), qMin(int(pl.size()), 320));
		s.writeDatagram(d, QHostAddress::LocalHost, srv);
	}
	void drain()
	{
		while (s.hasPendingDatagrams()) {
			const QByteArray d = s.receiveDatagram().data();
			if (d.size() < 32 || std::memcmp(d.constData(), "USRP", 4)) continue;
			const uchar *h = reinterpret_cast<const uchar *>(d.constData());
			const quint32 type = qFromBigEndian<quint32>(h + 20), keyup = qFromBigEndian<quint32>(h + 12);
			if (type == 0 && keyup && d.size() >= 352) {
				++voice;
				for (int i = 0; i < 160; ++i) peak = qMax(peak, qAbs(int(qFromLittleEndian<qint16>(h + 32 + i * 2))));
			}
			if (type == 2 && d.size() > 46 && uchar(d[32]) == 8) info_src.append((uchar(d[34]) << 16) | (uchar(d[35]) << 8) | uchar(d[36]));
		}
	}
};

// 두 클라이언트가 동시에 톤 송신
static void talk_both(Client &a, Client &b, int ms, double fa, double fb)
{
	double pa = 0, pb = 0;
	QElapsedTimer t; t.start();
	qint64 next = 0;
	while (t.elapsed() < ms) {
		if (t.elapsed() >= next) {
			QByteArray x(320, '\0'), y(320, '\0');
			for (int i = 0; i < 160; ++i) {
				qToLittleEndian<qint16>(qint16(8000 * std::sin(pa)), reinterpret_cast<uchar *>(x.data()) + i * 2); pa += 2 * M_PI * fa / 8000;
				qToLittleEndian<qint16>(qint16(8000 * std::sin(pb)), reinterpret_cast<uchar *>(y.data()) + i * 2); pb += 2 * M_PI * fb / 8000;
			}
			a.send(true, x); b.send(true, y);
			next += 20;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
		a.drain(); b.drain();
		QThread::msleep(1);
	}
	a.send(false, QByteArray()); b.send(false, QByteArray());
}

static void talk_one(Client &a, int ms)
{
	QElapsedTimer t; t.start();
	qint64 next = 0;
	while (t.elapsed() < ms) {
		if (t.elapsed() >= next) { a.send(true, QByteArray(320, '\0')); next += 20; }
		QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
		QThread::msleep(1);
	}
	a.send(false, QByteArray());
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	qRegisterMetaType<dmr::Dmrd>("dmr::Dmrd");
	if (argc < 3) { std::printf("usage: multiuser_test <fake_master.py> <plugins dir>\n"); return 2; }
	const QString mlog = QDir::tempPath() + "/dmrbridge_multi_master.log";
	QFile::remove(mlog);
	QProcess master;
	master.start("python", {"-I", QString::fromLocal8Bit(argv[1]), "40", "62091", "local", mlog});
	CHECK(master.waitForStarted(5000), "가짜 마스터 실행");
	pump(800);

	auto make = [&](const char *call, quint32 dmr, quint32 hotspot, quint16 usrp, int tg) {
		SessionConfig c;
		c.name = call; c.master.host = "127.0.0.1"; c.master.port = 62091; c.master.id = hotspot; c.master.callsign = call; c.master.password = "local";
		c.dmr_id = dmr; c.tg = tg; c.priv = false; c.usrp_port = usrp; c.plugin_dir = QString::fromLocal8Bit(argv[2]);
		return c;
	};
	Session s1(make("HL0EXM", 4500001, 450000199, 32091, 450));
	Session s2(make("HL1ABC", 4501234, 450123401, 32092, 91));
	CHECK(s1.start() && s2.start(), "세션 2개 시작 (USRP 32091, 32092)");
	CHECK(wait_until([&] { return s1.master()->connected() && s2.master()->connected(); }, 5000), "둘 다 마스터 접속");

	Client c1, c2;
	c1.s.bind(QHostAddress::LocalHost, 0); c1.srv = 32091;
	c2.s.bind(QHostAddress::LocalHost, 0); c2.srv = 32092;

	std::printf("A) 동시 송신 → 각자 자기 에코\n");
	talk_both(c1, c2, 3000, 1000, 600);
	wait_until([&] { c1.drain(); c2.drain(); return c1.voice > 100 && c2.voice > 100; }, 9000);
	pump(1500); c1.drain(); c2.drain();
	std::printf("     사용자1 에코 %d패킷, 사용자2 에코 %d패킷\n", c1.voice, c2.voice);
	QFile f(mlog); f.open(QIODevice::ReadOnly);
	const QString ml = QString::fromUtf8(f.readAll());
	CHECK(ml.contains("src=4500001 dst=450") && ml.contains("src=4501234 dst=91"), "마스터가 두 송신을 각각 받음 (4500001→450, 4501234→91)");
	CHECK(c1.voice > 100 && c1.voice < 200 && c2.voice > 100 && c2.voice < 200, "각 클라이언트는 자기 에코 한 개 분량만 받음 (섞이지 않음)");
	CHECK(!c1.info_src.isEmpty() && !c2.info_src.isEmpty() && c1.info_src.last() == 4500001 && c2.info_src.last() == 4501234, "수신 정보도 각자 자기 것");

	std::printf("B) 같은 DMR ID의 짧은 송신은 세션을 넘어 합산\n");
	Session s3(make("HL0EXM", 4500001, 450000101, 32093, 9990));	// 같은 사람, 접미사 01
	CHECK(s3.start() && wait_until([&] { return s3.master()->connected(); }, 5000), "같은 DMR ID 두 번째 핫스팟(접미사 01) 접속");
	Client c3; c3.s.bind(QHostAddress::LocalHost, 0); c3.srv = 32093;
	const int t1 = s1.tx_count(), t3 = s3.tx_count();
	for (int i = 0; i < 3; ++i) { talk_one(c1, 300); pump(300); }	// 사용자1(접미사 99)에서 짧게 3번
	for (int i = 0; i < 3; ++i) { talk_one(c3, 300); pump(300); }	// 같은 사람 접미사 01에서 짧게 3번
	std::printf("     접미사 99 송신 %d회, 접미사 01 송신 %d회, 합산 %d회\n", s1.tx_count() - t1, s3.tx_count() - t3, s1.short_tx_in_window());
	CHECK(s1.tx_count() - t1 == 3 && s3.tx_count() - t3 == 2, "합쳐 5회까지 송신, 6번째(다른 핫스팟)는 막음");
	CHECK(s1.short_tx_in_window() == 5 && s3.short_tx_in_window() == 5, "두 세션이 같은 집계(5회)를 봄");
	const int t2 = s2.tx_count();
	talk_one(c2, 300); pump(300);
	CHECK(s2.tx_count() == t2 + 1, "다른 DMR ID 사용자는 영향 없음");

	s1.stop(); s2.stop(); s3.stop();
	master.waitForFinished(45000);
	std::printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS\n");
	return g_fail ? 1 : 0;
}
