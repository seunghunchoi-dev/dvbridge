// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// dvbridge_cli — 설정 파일(INI)로 브릿지 세션 하나를 실행하는 간이 실행기 (화면 버전 전까지 사용)
// 사용: dvbridge_cli <설정.ini> [실행 초, 0=무한] [--notice-after <초> "<문장>"]
//   --notice-after: 시작 후 <초> 뒤 문장을 음성 합성해 브릿지 공지로 한 번 보냄 (클라이언트 시험용, BM으로는 안 나감)
//
// [master]  host, port, id(핫스팟 ID), password(비우면 환경변수 DVB_PW), callsign, options
// [user]    dmr_id, tg, private(0/1), slot, cc
// [usrp]    port(앱의 TX Port, 기본 32001), tot(초, 기본 180)
// [vocoder] plugins(플러그인 폴더, 비우면 실행파일 옆 plugins)
#include <cstdio>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QTimer>
#include "notice_service.h"
#include "session/session.h"
#include "session/id_db.h"
#include "net/id_db_updater.h"
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	qRegisterMetaType<dmr::Dmrd>("dmr::Dmrd");
	std::printf("DVBridge (MIT) - Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)\n");
	if (argc < 2) { std::printf("usage: dvbridge_cli <config.ini> [seconds] [--notice-after <seconds> \"<text>\"]\n       dvbridge_cli --download-ids <저장 경로>\n"); return 2; }
	if (QString(argv[1]) == "--download-ids" && argc >= 3) {	// RadioID 명단 받기 (화면의 '명단 받기'와 같은 코드)
		IdDbUpdater up;
		int rc = 1;
		QObject::connect(&up, &IdDbUpdater::progress, [](qint64 got, qint64 total) {
			static int last = -1;
			const int pct = total > 0 ? int(got * 100 / total) : -1;
			if (pct / 10 != last / 10) { last = pct; std::printf("  %d%%\n", pct); std::fflush(stdout); }
		});
		QObject::connect(&up, &IdDbUpdater::finished, [&](bool ok, QString msg) {
			std::printf("%s\n", msg.toUtf8().constData());
			rc = ok ? 0 : 1;
			QCoreApplication::quit();
		});
		up.start(QString::fromLocal8Bit(argv[2]));
		app.exec();
		IdDatabase db;
		if (rc == 0 && db.load(QString::fromLocal8Bit(argv[2]))) std::printf("확인: %d명\n", db.size());
		return rc;
	}
	QSettings ini(QString::fromLocal8Bit(argv[1]), QSettings::IniFormat);

	SessionConfig c;
	c.master.host = ini.value("master/host", "4501.master.brandmeister.network").toString();
	c.master.port = quint16(ini.value("master/port", 62031).toUInt());
	c.master.id = ini.value("master/id").toUInt();
	c.master.password = ini.value("master/password").toString();
	if (c.master.password.isEmpty()) c.master.password = qEnvironmentVariable("DVB_PW");
	c.master.callsign = ini.value("master/callsign").toString();
	c.master.options = ini.value("master/options").toString();
	c.name = c.master.callsign;
	c.dmr_id = ini.value("user/dmr_id").toUInt();
	c.tg = ini.value("user/tg", 9990).toInt();
	c.priv = ini.value("user/private", 1).toInt() != 0;
	c.slot = ini.value("user/slot", 2).toInt();
	c.cc = ini.value("user/cc", 1).toInt();
	c.device = ini.value("user/device").toString();
	c.device_relogin_min_s = ini.value("user/device_relogin_min_s", 600).toInt();
	c.usrp_port = quint16(ini.value("usrp/port", 32001).toUInt());
	c.tot_seconds = ini.value("usrp/tot", 180).toInt();
	c.plugin_dir = ini.value("vocoder/plugins").toString();
	if (c.plugin_dir.isEmpty()) c.plugin_dir = QCoreApplication::applicationDirPath() + "/plugins";

	if (!c.master.id || !c.dmr_id || c.master.callsign.isEmpty() || c.master.password.isEmpty()) {
		std::printf("설정 부족: master/id, master/callsign, user/dmr_id, 비밀번호(master/password 또는 DVB_PW) 필요\n");
		return 2;
	}
	Session s(c);
	QObject::connect(&s, &Session::log, [](QString l) {
		std::printf("%s %s\n", QDateTime::currentDateTime().toString("HH:mm:ss").toUtf8().constData(), l.toUtf8().constData());
		std::fflush(stdout);
	});
	if (!s.start()) return 1;
	const int secs = argc > 2 ? QString(argv[2]).toInt() : 0;
	if (secs > 0) QTimer::singleShot(secs * 1000, &app, [&] { s.stop(); QTimer::singleShot(500, &app, &QCoreApplication::quit); });
	NoticeService notice;
	for (int i = 2; i + 2 < argc; ++i) {
		if (QString(argv[i]) != "--notice-after") continue;
		const int after = QString(argv[i + 1]).toInt();
		const QString text = QString::fromLocal8Bit(argv[i + 2]);
		QObject::connect(&notice, &NoticeService::log, [](QString l) { std::printf("%s\n", l.toUtf8().constData()); std::fflush(stdout); });
		QObject::connect(&notice, &NoticeService::ready, &s, [&s](QVector<int16_t> pcm, QString title, QString, QStringList, bool ai) { s.notice_add(title, "tts", pcm, ai); });
		QTimer::singleShot(qMax(0, after) * 1000, &app, [&notice, text] { NoticeService::Job j; j.text = text; notice.speak(j); });
		break;
	}
	return app.exec();
}
