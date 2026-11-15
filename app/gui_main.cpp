// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// 실행 방법
//   DVBridge.exe                 창을 띄움
//   DVBridge.exe --tray          트레이로 시작 (Windows 자동 실행)
//   DVBridge.exe --quit          실행 중인 DVBridge를 정상 종료 (업데이트 설치 전)
//   DVBridge.exe --watchdog PID  (내부용) PID 프로세스가 비정상 종료되면 다시 실행
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include "app_settings.h"
#include "main_window.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void log_line(const QString &s)
{
	const QDateTime now = QDateTime::currentDateTime();
	const QString dir = QDir(AppSettings::config_dir()).filePath("logs");
	QDir().mkpath(dir);
	QFile f(QDir(dir).filePath(now.toString("yyyy-MM-dd") + ".log"));
	if (f.open(QIODevice::Append | QIODevice::Text)) f.write((now.toString("HH:mm:ss ") + s + "\n").toUtf8());
}

// 감시 프로세스: 지켜보는 DVBridge가 끝날 때까지 기다렸다가, 정상 종료 표시가 없으면 다시 실행
int run_watchdog(qint64 pid)
{
#ifdef _WIN32
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
	if (!h) return 0;
	WaitForSingleObject(h, INFINITE);
	CloseHandle(h);
#endif
	if (QFile::exists(MainWindow::clean_exit_flag())) return 0;
	if (!AppSettings::load().watchdog) return 0;
	// 다시 실행이 반복되면(10분 안에 3번) 멈춤 — 설정 문제로 계속 죽는 경우
	const QString hist = QDir(AppSettings::config_dir()).filePath("watchdog_restarts.txt");
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	QStringList keep;
	QFile f(hist);
	if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		for (const QString &l : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts))
			if (now - l.toLongLong() < 600) keep << l;
		f.close();
	}
	if (keep.size() >= 3) { log_line("와치독: 10분 안에 3번 다시 실행해도 멈춰서 더는 다시 실행하지 않습니다"); return 0; }
	keep << QString::number(now);
	if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) f.write((keep.join('\n') + '\n').toUtf8());
	log_line("와치독: DVBridge가 비정상 종료되어 3초 뒤 다시 실행합니다");
#ifdef _WIN32
	Sleep(3000);
#endif
	QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--tray", "--recover"});
	return 0;
}
}

int main(int argc, char **argv)
{
	QStringList args;
	for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
	const int wi = int(args.indexOf("--watchdog"));
	if (wi >= 0 && wi + 1 < args.size()) {
		QCoreApplication core(argc, argv);
		return run_watchdog(args[wi + 1].toLongLong());
	}

	QApplication app(argc, argv);
	QApplication::setApplicationName("DVBridge");
	QApplication::setOrganizationName("DVBridge");
	QApplication::setWindowIcon(QIcon(":/dvbridge.png"));
	QApplication::setQuitOnLastWindowClosed(false);		// 창을 닫아도 트레이에서 계속
	QFont font = app.font();
	font.setPointSize(11);									// 큰 글씨
	app.setFont(font);
	qRegisterMetaType<dmr::Dmrd>("dmr::Dmrd");

	// 한 번에 하나만 실행: 이미 떠 있으면 그 창을 앞으로 불러오고 끝냄
	const QString key = "DVBridge-" + QString::fromLatin1(QCryptographicHash::hash(AppSettings::config_dir().toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
	QLockFile lock(QDir(AppSettings::config_dir()).filePath("dvbridge.lock"));
	lock.setStaleLockTime(0);	// 비정상 종료로 남은 잠금은 프로세스가 없으면 자동 해제
	const bool quit = args.contains("--quit");
	if (!lock.tryLock(200)) {
		QLocalSocket sock;
		sock.connectToServer(key);
		if (sock.waitForConnected(1000)) {
			sock.write(quit ? "quit" : "show");
			sock.waitForBytesWritten(1000);
			if (quit) sock.waitForDisconnected(5000);	// 종료가 끝날 때까지 잠깐 기다림
			return 0;
		}
		if (!quit) QMessageBox::information(nullptr, "DVBridge", "DVBridge가 이미 실행 중입니다.\n작업 표시줄 오른쪽(트레이) 아이콘을 눌러 주세요.");
		return 0;
	}
	if (quit) return 0;		// 실행 중인 DVBridge가 없음

#ifdef _WIN32
	// 설치 프로그램이 "실행 중"을 알아보는 표시 (있을 때만 --quit로 정상 종료를 요청)
	HANDLE running_mutex = CreateMutexW(nullptr, FALSE, L"DVBridgeRunning");
	Q_UNUSED(running_mutex);
#endif
	MainWindow w(args.contains("--tray"), args.contains("--recover"));
	QLocalServer::removeServer(key);
	QLocalServer server;
	server.listen(key);
	QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
		while (QLocalSocket *c = server.nextPendingConnection()) {
			QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
			QObject::connect(c, &QLocalSocket::readyRead, &w, [&w, c] {
				if (c->readAll().contains("quit")) { w.quit_now(true); return; }	// 업데이트 설치 전 정상 종료
				w.showNormal(); w.raise(); w.activateWindow();
			});
		}
	});
	return app.exec();
}
