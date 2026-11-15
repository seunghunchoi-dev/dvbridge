// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// 이력(CallLog)·웹 대시보드 시험 (127.0.0.1만 사용)
#include <cstdio>
#include <functional>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include "session/call_log.h"
#include "web/dashboard_server.h"
#include "web/sys_monitor.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

static QByteArray http(quint16 port, const QByteArray &req)
{
	QTcpSocket s;
	s.connectToHost(QHostAddress::LocalHost, port);
	QByteArray out;
	QElapsedTimer t; t.start();
	bool sent = false;
	while (t.elapsed() < 3000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		if (!sent && s.state() == QAbstractSocket::ConnectedState) { s.write(req); sent = true; }
		s.waitForReadyRead(10);
		out += s.readAll();
		if (sent && s.state() == QAbstractSocket::UnconnectedState) break;
	}
	return out;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	QTemporaryDir tmp;
	const QString file = tmp.filePath("history.jsonl");
	CallLog log;
	log.set_file(file, 3);
	for (int i = 0; i < 4; ++i) {
		CallRecord r;
		r.start_ms = 1794650000000LL + i * 1000; r.duration_ms = 3000 + i; r.tx = (i % 2 == 0);
		r.src = 4500001; r.dst = i == 3 ? 4000 : 9990; r.priv = i != 3; r.kind = i == 3 ? "unlink" : "voice"; r.src_call = "HL0EXM";
		log.add(r);
	}
	CHECK(log.size() == 3, "최근 3건만 유지 (keep=3)");
	CallLog log2;
	log2.set_file(file, 3);
	CHECK(log2.size() == 3 && log2.records().last().kind == "unlink", "파일에서 다시 불러오기 (최근 3건)");

	DashboardServer web(&log);
	web.set_status_provider([] { QJsonObject o; o["target"] = 450997; o["target_priv"] = true; o["connected"] = true; return o; });
	CHECK(web.start(0, false), "대시보드 서버 시작 (127.0.0.1)");
	const quint16 port = web.port();

	const QByteArray page = http(port, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
	CHECK(page.startsWith("HTTP/1.1 200") && page.contains("DVBridge") && page.contains("최초 제작"), "GET / → 대시보드 페이지 (제작자 표기 포함)");
	const QByteArray hist = http(port, "GET /api/history HTTP/1.1\r\n\r\n");
	const QJsonArray arr = QJsonDocument::fromJson(hist.mid(hist.indexOf("\r\n\r\n") + 4)).array();
	CHECK(arr.size() == 3 && arr[0].toObject()["kind"].toString() == "unlink", "GET /api/history → 3건, 최신순");
	const QByteArray st = http(port, "GET /api/status HTTP/1.1\r\n\r\n");
	CHECK(st.contains("450997") && !st.contains("password"), "GET /api/status → 상태 (비밀번호 없음)");
	CHECK(http(port, "POST /api/history HTTP/1.1\r\nContent-Length: 0\r\n\r\n").startsWith("HTTP/1.1 405"), "POST 거부 (보기 전용)");
	CHECK(http(port, "GET /../../windows HTTP/1.1\r\n\r\n").startsWith("HTTP/1.1 404"), "다른 경로 거부");

	// ---- 실시간 듣기 스트림 ----
	{
		QList<QTcpSocket *> socks;
		for (int i = 0; i < 5; ++i) {
			QTcpSocket *t = new QTcpSocket;
			t->connectToHost(QHostAddress::LocalHost, port);
			t->waitForConnected(1000);
			t->write("GET /api/listen?u=0 HTTP/1.1\r\n\r\n");
			socks.append(t);
		}
		QElapsedTimer w; w.start();
		// PC가 바쁠 때도 안정적으로: 4명 연결 + 5번째 응답 도착 + 첫 청취자 응답 머리 도착까지 (최대 8초 - 빌드 중 바쁠 때 3초로는 모자란 적 있음)
		QByteArray fifth_buf, head_buf;
		while (w.elapsed() < 8000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
			for (auto *t : socks) t->waitForReadyRead(5);
			fifth_buf += socks[4]->readAll();
			head_buf += socks[0]->readAll();
			if (web.listener_count() == 4 && fifth_buf.contains("\r\n\r\n") && head_buf.contains("\r\n\r\n")) break;
		}
		CHECK(web.listener_count() == 4, "사용자당 청취자 4명까지만 연결");
		CHECK(fifth_buf.startsWith("HTTP/1.1 429"), "5번째 청취자는 429로 거부");
		CHECK(head_buf.startsWith("HTTP/1.1 200") && head_buf.contains("pcm_s16le;rate=8000"), "청취 응답 머리: 8kHz 16bit PCM");
		QByteArray pcm(320, '\0');
		for (int i = 0; i < 160; ++i) { pcm[i * 2] = char(i); pcm[i * 2 + 1] = 0x10; }
		web.push_audio(0, pcm);
		web.push_audio(1, QByteArray(320, 'x'));	// 다른 사용자 음성은 섞이면 안 됨
		QByteArray got;
		w.restart();
		while (w.elapsed() < 1000 && got.size() < 320) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); socks[1]->waitForReadyRead(10); got += socks[1]->readAll(); }
		const QByteArray body = got.mid(got.indexOf("\r\n\r\n") >= 0 ? got.indexOf("\r\n\r\n") + 4 : 0);
		CHECK(body == pcm, "밀어 넣은 음성 320바이트가 그대로 청취자에게 전달 (다른 사용자 음성 섞이지 않음)");
		for (auto *t : socks) { t->abort(); delete t; }
		w.restart();
		while (w.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		CHECK(web.listener_count() == 0, "청취자가 끊으면 목록에서 빠짐");
	}

	// ---- 웹 설정 (관리자) ----
	{
		QString admin_pw;			// 가짜 저장소
		int actions = 0;
		DashboardServer::AdminHooks h;
		h.has_password = [&] { return !admin_pw.isEmpty(); };
		h.check_password = [&](const QString &p) { return !admin_pw.isEmpty() && p == admin_pw; };
		h.set_password = [&](const QString &p) { admin_pw = p; };
		h.config = [] { QJsonObject o; o["users"] = QJsonArray{QJsonObject{{"callsign", "HL0TEST"}, {"password_set", true}}}; return o; };
		h.action = [&](const QString &a, const QJsonObject &) { ++actions; return QJsonObject{{"ok", a == "stop_all"}, {"message", a}}; };
		web.set_admin_hooks(h);
		auto post = [&](const QByteArray &path, const QByteArray &json, const QByteArray &cookie = QByteArray(), bool csrf = true) {
			QByteArray req = "POST " + path + " HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(json.size()) + "\r\n";
			if (csrf) req += "X-DVBridge: 1\r\n";
			if (!cookie.isEmpty()) req += "Cookie: " + cookie + "\r\n";
			return http(port, req + "\r\n" + json);
		};
		auto get = [&](const QByteArray &path, const QByteArray &cookie = QByteArray()) {
			return http(port, "GET " + path + " HTTP/1.1\r\n" + (cookie.isEmpty() ? QByteArray() : "Cookie: " + cookie + "\r\n") + "\r\n");
		};
		auto cookie_of = [](const QByteArray &resp) {
			const int i = resp.indexOf("dvb_session=");
			return i < 0 ? QByteArray() : resp.mid(i, resp.indexOf(';', i) - i);
		};
		CHECK(get("/settings").startsWith("HTTP/1.1 200") && get("/settings").contains("관리자"), "GET /settings → 설정 페이지");
		CHECK(get("/api/admin/state").contains("\"has_password\":false") && get("/api/admin/state").contains("\"local\":true"), "처음: 관리자 비밀번호 없음, 이 PC에서 접속");
		CHECK(post("/api/admin/setup", "{\"password\":\"abcdef\"}", QByteArray(), false).startsWith("HTTP/1.1 403"), "X-DVBridge 머리 없는 POST 거부 (요청 위조 차단)");
		CHECK(post("/api/admin/setup", "{\"password\":\"abc\"}").startsWith("HTTP/1.1 400"), "짧은 관리자 비밀번호 거부");
		const QByteArray setup = post("/api/admin/setup", "{\"password\":\"secret77\"}");
		const QByteArray c1 = cookie_of(setup);
		CHECK(setup.startsWith("HTTP/1.1 200") && !c1.isEmpty() && setup.contains("HttpOnly") && setup.contains("SameSite=Strict"), "첫 설정 → 세션 쿠키 (HttpOnly, SameSite=Strict)");
		CHECK(post("/api/admin/setup", "{\"password\":\"another1\"}").startsWith("HTTP/1.1 409"), "이미 있으면 다시 설정 불가");
		CHECK(get("/api/admin/config").startsWith("HTTP/1.1 401"), "로그인 없이 설정 보기 거부");
		CHECK(get("/api/admin/config", c1).startsWith("HTTP/1.1 200"), "로그인 쿠키로 설정 보기");
		CHECK(post("/api/admin/action", "{\"action\":\"stop_all\"}").startsWith("HTTP/1.1 401") && actions == 0, "로그인 없이 설정 변경 거부");
		CHECK(post("/api/admin/action", "{\"action\":\"stop_all\"}", c1).startsWith("HTTP/1.1 200") && actions == 1, "로그인 상태에서 동작 요청 전달");
		CHECK(post("/api/admin/action", "{\"action\":\"change_password\",\"old\":\"wrong\",\"new\":\"newpass9\"}", c1).startsWith("HTTP/1.1 401"), "현재 비밀번호가 틀리면 변경 거부");
		const QByteArray chg = post("/api/admin/action", "{\"action\":\"change_password\",\"old\":\"secret77\",\"new\":\"newpass9\"}", c1);
		const QByteArray c2 = cookie_of(chg);
		CHECK(chg.startsWith("HTTP/1.1 200") && admin_pw == "newpass9" && get("/api/admin/config", c1).startsWith("HTTP/1.1 401") && get("/api/admin/config", c2).startsWith("HTTP/1.1 200"),
			  "관리자 비밀번호 변경 → 이전 로그인은 끊기고 새 쿠키만 유효");
		CHECK(post("/api/admin/login", "{\"password\":\"newpass9\"}").startsWith("HTTP/1.1 200"), "올바른 비밀번호로 로그인");
		int r401 = 0;
		for (int i = 0; i < 5; ++i) r401 += post("/api/admin/login", "{\"password\":\"bad\"}").startsWith("HTTP/1.1 401");
		const QByteArray locked = post("/api/admin/login", "{\"password\":\"newpass9\"}");
		CHECK(r401 == 5 && locked.startsWith("HTTP/1.1 429"), "5번 틀리면 맞는 비밀번호도 5분간 잠금");
		const QByteArray out = post("/api/admin/logout", "{}", c2);
		CHECK(out.contains("Max-Age=0") && get("/api/admin/config", c2).startsWith("HTTP/1.1 401"), "로그아웃하면 쿠키 무효");
		// 본문 길이를 64KB 넘게 선언하면 본문을 받기 전에 거부 (본문을 실제로 다 보내면 시험 쪽 소켓이 먼저 끊겨 응답을 못 읽음)
		const QByteArray big = http(port, "POST /api/admin/login HTTP/1.1\r\nX-DVBridge: 1\r\nContent-Length: " + QByteArray::number(DashboardServer::kMaxBody + 10) + "\r\n\r\n");
		CHECK(big.startsWith("HTTP/1.1 413"), "64KB 넘는 요청 거부");
		CHECK(get("/").contains("/settings"), "대시보드에 설정 링크");
	}

	// ---- 서버 부하 ----
	{
		SysMonitor m;
		QThread::msleep(300);
		m.sample();
		const QJsonObject o = m.to_json();
		CHECK(o["proc_mem_mb"].toDouble() > 1 && o["sys_mem_gb"].toDouble() > 0.5 && o["cores"].toInt() >= 1, "메모리·코어 수 측정");
		CHECK(o["proc_cpu"].toDouble() >= 0 && o["proc_cpu"].toDouble() <= 100 && o["sys_cpu"].toDouble() >= 0 && o["sys_cpu"].toDouble() <= 100, "CPU 사용률 0~100%");
	}

	std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	return g_fail ? 1 : 0;
}
