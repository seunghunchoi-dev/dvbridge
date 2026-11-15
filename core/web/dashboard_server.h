// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// DashboardServer — 웹 대시보드(보기) + 웹 설정(관리자 로그인)
//   보기 (누구나):
//     GET /                 대시보드 페이지
//     GET /api/status       현재 상태 JSON (비밀번호 등 설정값 없음)
//     GET /api/history      이력 JSON (최신순)
//     GET /api/listen?u=N   사용자 N의 실시간 음성 (8kHz 16bit 모노 PCM 스트림, 사용자당 4명까지)
//   설정 (관리자):
//     GET  /settings              설정 페이지
//     GET  /api/admin/state       {has_password, logged_in, local}
//     POST /api/admin/setup       첫 관리자 비밀번호 설정 — 이 PC(127.0.0.1)에서만
//     POST /api/admin/login       로그인 (5번 틀리면 그 주소는 5분 잠금)
//     POST /api/admin/logout
//     GET  /api/admin/config      설정 (핫스팟 비밀번호는 내보내지 않음)
//     GET  /api/admin/backup      설정 백업 파일 내려받기 (비밀번호는 이 PC·Windows 계정에서만 풀리는 암호문)
//     POST /api/admin/action      {action: save_user|delete_user|start_all|stop_all|download_ids|restore|change_password, ...}
//   보안: 세션 쿠키(HttpOnly, SameSite=Strict, 12시간), 모든 POST는 X-DVBridge 머리 필수(다른 사이트의 요청 위조 차단),
//         요청 본문 64KB 제한. HTTPS가 아니므로 비밀번호 입력은 이 PC나 믿을 수 있는 네트워크에서.
#pragma once
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <functional>

class QTcpServer;
class QTcpSocket;
class CallLog;

class DashboardServer : public QObject
{
	Q_OBJECT
public:
	struct AdminHooks {
		std::function<bool()> has_password;
		std::function<bool(const QString &)> check_password;
		std::function<void(const QString &)> set_password;		// 저장까지
		std::function<QJsonObject()> config;
		// 반환 {"ok": bool, "message": "…"}
		std::function<QJsonObject(const QString &action, const QJsonObject &body)> action;
		std::function<QByteArray()> backup;			// 현재 설정 파일 내용 (내려받기)
		std::function<QByteArray()> notice_wav;		// 마지막으로 만든 공지 음성 (WAV, 웹에서 PC 스피커로 미리 듣기)
	};

	explicit DashboardServer(CallLog *log, QObject *parent = nullptr);
	bool start(quint16 port, bool lan);		// lan=false면 이 PC(127.0.0.1)에서만
	bool start(const QHostAddress &bind, quint16 port);	// 0.0.0.0 = 모든 네트워크, 127.0.0.1 = 이 PC만, 그 밖 = 그 IP만
	void stop();
	quint16 port() const;
	bool listening() const;
	void set_status_provider(std::function<QJsonObject()> f) { status_ = std::move(f); }
	void set_admin_hooks(AdminHooks h) { admin_ = std::move(h); }
	static QByteArray page_html();
	static QByteArray settings_html();
	void push_audio(int user, const QByteArray &pcm);	// 세션 음성 → 청취자에게 전달
	int listener_count() const;
	static constexpr int kMaxListenersPerUser = 4;
	static constexpr int kMaxBody = 64 * 1024;

private slots:
	void on_new_connection();

private:
	struct Request {
		QByteArray method, path, query, body, cookie;
		bool csrf_ok = false;		// X-DVBridge 머리 있음
		bool local = false;			// 127.0.0.1에서 옴
		QString peer;
	};
	void handle(QTcpSocket *s, const Request &r);
	void handle_admin(QTcpSocket *s, const Request &r);
	void reply(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, const QByteArray &extra = QByteArray());
	void reply_json(QTcpSocket *s, int code, const QJsonObject &o, const QByteArray &extra = QByteArray());
	bool logged_in(const Request &r);
	QByteArray new_session();		// Set-Cookie 머리 반환

	QTcpServer *server_ = nullptr;
	CallLog *log_ = nullptr;
	std::function<QJsonObject()> status_;
	AdminHooks admin_;
	QHash<int, QList<QTcpSocket *>> listeners_;
	QHash<QByteArray, qint64> sessions_;			// 토큰 → 만료 시각(ms)
	struct Fail { int count = 0; qint64 until = 0; };
	QHash<QString, Fail> fails_;					// 주소별 로그인 실패
};
