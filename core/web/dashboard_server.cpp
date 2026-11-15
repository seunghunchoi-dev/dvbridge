// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "dashboard_server.h"
#include "session/call_log.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

namespace {
constexpr qint64 kSessionMs = 12LL * 3600 * 1000;
constexpr int kMaxFails = 5;
constexpr qint64 kLockMs = 5LL * 60 * 1000;
constexpr int kMinPassword = 6;

QByteArray header_value(const QByteArray &head, const QByteArray &name)
{
	for (const QByteArray &line : head.split('\n')) {
		const int c = line.indexOf(':');
		if (c > 0 && line.left(c).trimmed().toLower() == name) return line.mid(c + 1).trimmed();
	}
	return QByteArray();
}
}

DashboardServer::DashboardServer(CallLog *log, QObject *parent) : QObject(parent), log_(log) {}

bool DashboardServer::start(quint16 port, bool lan)
{
	return start(lan ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(QHostAddress::LocalHost), port);
}

bool DashboardServer::start(const QHostAddress &bind, quint16 port)
{
	if (!server_) {
		server_ = new QTcpServer(this);
		connect(server_, &QTcpServer::newConnection, this, &DashboardServer::on_new_connection);
	}
	return server_->listen(bind, port);
}

void DashboardServer::stop() { if (server_) server_->close(); }
quint16 DashboardServer::port() const { return server_ ? server_->serverPort() : 0; }
bool DashboardServer::listening() const { return server_ && server_->isListening(); }

void DashboardServer::on_new_connection()
{
	while (server_->hasPendingConnections()) {
		QTcpSocket *s = server_->nextPendingConnection();
		connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
		connect(s, &QTcpSocket::readyRead, this, [this, s] {
			if (s->property("done").toBool()) return;
			// 요청 머리(빈 줄까지) + 본문(Content-Length)이 다 오면 처리
			const QByteArray buf = s->peek(8192 + kMaxBody + 16);
			const int he = buf.indexOf("\r\n\r\n");
			if (he < 0) { if (s->bytesAvailable() > 8192) s->abort(); return; }
			const QByteArray head = buf.left(he);
			const int len = header_value(head, "content-length").toInt();
			if (len < 0 || len > kMaxBody) { s->setProperty("done", true); reply(s, 413, "text/plain", "too large"); return; }
			if (s->bytesAvailable() < he + 4 + len) return;
			s->setProperty("done", true);
			const QByteArray all = s->read(he + 4 + len);
			Request r;
			const int eol = head.indexOf("\r\n");		// 머리 줄이 없으면 요청 줄 = 머리 전체
			const QList<QByteArray> line = (eol < 0 ? head : head.left(eol)).split(' ');
			if (line.size() < 2) { reply(s, 400, "text/plain", "bad request"); return; }
			r.method = line[0];
			r.path = line[1];
			const int q = r.path.indexOf('?');
			if (q >= 0) { r.query = r.path.mid(q + 1); r.path = r.path.left(q); }
			r.body = all.mid(he + 4, len);
			r.cookie = header_value(head, "cookie");
			r.csrf_ok = header_value(head, "x-dmrbridge") == "1";
			r.local = s->peerAddress().isLoopback();
			r.peer = s->peerAddress().toString();
			handle(s, r);
		});
	}
}

void DashboardServer::handle(QTcpSocket *s, const Request &r)
{
	if (r.path == "/settings" || r.path.startsWith("/api/admin/")) { handle_admin(s, r); return; }
	if (r.method != "GET") { reply(s, 405, "text/plain", "read-only"); return; }
	if (r.path == "/api/listen") {
		int user = -1;
		for (const QByteArray &kv : r.query.split('&')) if (kv.startsWith("u=")) user = kv.mid(2).toInt();
		QList<QTcpSocket *> &l = listeners_[user];
		if (user < 0 || l.size() >= kMaxListenersPerUser) { reply(s, 429, "text/plain", "too many listeners"); return; }
		// 길이를 정하지 않은 스트림: 연결이 끊길 때까지 PCM을 계속 씀
		s->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\n"
				 "X-Audio-Format: pcm_s16le;rate=8000;channels=1\r\nConnection: close\r\n\r\n");
		l.append(s);
		connect(s, &QTcpSocket::disconnected, this, [this, user, s] { listeners_[user].removeAll(s); });
		return;
	}
	if (r.path == "/" || r.path == "/index.html") reply(s, 200, "text/html; charset=utf-8", page_html());
	else if (r.path == "/api/history") reply(s, 200, "application/json", QJsonDocument(log_ ? log_->to_json(200) : QJsonArray()).toJson(QJsonDocument::Compact));
	else if (r.path == "/api/status") reply(s, 200, "application/json", QJsonDocument(status_ ? status_() : QJsonObject()).toJson(QJsonDocument::Compact));
	else reply(s, 404, "text/plain", "not found");
}

bool DashboardServer::logged_in(const Request &r)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = sessions_.begin(); it != sessions_.end();) it = it.value() < now ? sessions_.erase(it) : std::next(it);
	for (const QByteArray &part : r.cookie.split(';')) {
		const QByteArray kv = part.trimmed();
		if (kv.startsWith("dmrb_session=") && sessions_.contains(kv.mid(13))) return true;
	}
	return false;
}

QByteArray DashboardServer::new_session()
{
	QByteArray raw(32, '\0');
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), 8);
	const QByteArray tok = raw.toHex();
	sessions_.insert(tok, QDateTime::currentMSecsSinceEpoch() + kSessionMs);
	return "Set-Cookie: dmrb_session=" + tok + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200\r\n";
}

void DashboardServer::handle_admin(QTcpSocket *s, const Request &r)
{
	const bool has = admin_.has_password && admin_.has_password();
	if (r.path == "/settings") {
		if (r.method != "GET") { reply(s, 405, "text/plain", "GET only"); return; }
		reply(s, 200, "text/html; charset=utf-8", settings_html());
		return;
	}
	if (r.method == "GET" && r.path == "/api/admin/state") {
		QJsonObject o;
		o["has_password"] = has;
		o["logged_in"] = logged_in(r);
		o["local"] = r.local;
		reply_json(s, 200, o);
		return;
	}
	if (r.method == "GET" && r.path == "/api/admin/config") {
		if (!logged_in(r)) { reply_json(s, 401, QJsonObject{{"ok", false}, {"message", "로그인이 필요합니다"}}); return; }
		reply_json(s, 200, admin_.config ? admin_.config() : QJsonObject());
		return;
	}
	if (r.method == "GET" && r.path == "/api/admin/backup") {
		if (!logged_in(r)) { reply_json(s, 401, QJsonObject{{"ok", false}, {"message", "로그인이 필요합니다"}}); return; }
		const QByteArray name = "DMRBridge-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmm").toLatin1() + ".ini";
		reply(s, 200, "application/octet-stream", admin_.backup ? admin_.backup() : QByteArray(),
			  "Content-Disposition: attachment; filename=\"" + name + "\"\r\n");
		return;
	}
	if (r.method == "GET" && r.path == "/api/admin/notice.wav") {
		if (!logged_in(r)) { reply_json(s, 401, QJsonObject{{"ok", false}, {"message", "로그인이 필요합니다"}}); return; }
		const QByteArray w = admin_.notice_wav ? admin_.notice_wav() : QByteArray();
		if (w.isEmpty()) reply(s, 404, "text/plain", "no notice audio");
		else reply(s, 200, "audio/wav", w, "Cache-Control: no-store\r\n");
		return;
	}
	if (r.method != "POST") { reply(s, 404, "text/plain", "not found"); return; }
	if (!r.csrf_ok) { reply_json(s, 403, QJsonObject{{"ok", false}, {"message", "허용되지 않은 요청"}}); return; }
	const QJsonObject body = QJsonDocument::fromJson(r.body).object();
	auto bad = [&](int code, const QString &m) { reply_json(s, code, QJsonObject{{"ok", false}, {"message", m}}); };

	if (r.path == "/api/admin/setup") {
		if (has) { bad(409, "이미 관리자 비밀번호가 있습니다"); return; }
		if (!r.local) { bad(403, "처음 관리자 비밀번호는 DMRBridge가 실행 중인 PC에서 정해 주세요 (http://localhost)"); return; }
		const QString pw = body["password"].toString();
		if (pw.size() < kMinPassword) { bad(400, QString("비밀번호는 %1자 이상").arg(kMinPassword)); return; }
		if (admin_.set_password) admin_.set_password(pw);
		reply_json(s, 200, QJsonObject{{"ok", true}}, new_session());
		return;
	}
	if (r.path == "/api/admin/login") {
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		Fail &f = fails_[r.peer];
		if (f.until > now) { bad(429, QString("로그인을 너무 많이 틀렸습니다. %1분 뒤에 다시 시도하세요").arg((f.until - now) / 60000 + 1)); return; }
		if (!has) { bad(409, "관리자 비밀번호가 아직 없습니다"); return; }
		if (!admin_.check_password || !admin_.check_password(body["password"].toString())) {
			if (++f.count >= kMaxFails) { f.until = now + kLockMs; f.count = 0; }
			bad(401, "비밀번호가 틀렸습니다");
			return;
		}
		fails_.remove(r.peer);
		reply_json(s, 200, QJsonObject{{"ok", true}}, new_session());
		return;
	}
	if (r.path == "/api/admin/logout") {
		for (const QByteArray &part : r.cookie.split(';')) {
			const QByteArray kv = part.trimmed();
			if (kv.startsWith("dmrb_session=")) sessions_.remove(kv.mid(13));
		}
		reply_json(s, 200, QJsonObject{{"ok", true}}, "Set-Cookie: dmrb_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\n");
		return;
	}
	if (r.path == "/api/admin/action") {
		if (!logged_in(r)) { bad(401, "로그인이 필요합니다"); return; }
		const QString action = body["action"].toString();
		if (action == "change_password") {
			if (!admin_.check_password || !admin_.check_password(body["old"].toString())) { bad(401, "현재 비밀번호가 틀렸습니다"); return; }
			const QString pw = body["new"].toString();
			if (pw.size() < kMinPassword) { bad(400, QString("새 비밀번호는 %1자 이상").arg(kMinPassword)); return; }
			if (admin_.set_password) admin_.set_password(pw);
			sessions_.clear();		// 다른 기기의 로그인은 모두 끊음
			reply_json(s, 200, QJsonObject{{"ok", true}, {"message", "관리자 비밀번호를 바꿨습니다"}}, new_session());
			return;
		}
		const QJsonObject res = admin_.action ? admin_.action(action, body) : QJsonObject{{"ok", false}, {"message", "지원하지 않음"}};
		reply_json(s, res["ok"].toBool() ? 200 : 400, res);
		return;
	}
	reply(s, 404, "text/plain", "not found");
}

void DashboardServer::reply_json(QTcpSocket *s, int code, const QJsonObject &o, const QByteArray &extra)
{
	reply(s, code, "application/json; charset=utf-8", QJsonDocument(o).toJson(QJsonDocument::Compact), extra);
}

void DashboardServer::reply(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, const QByteArray &extra)
{
	const char *text = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden"
		: code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : code == 409 ? "Conflict" : code == 413 ? "Payload Too Large"
		: code == 429 ? "Too Many Requests" : "Error";
	QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + text + "\r\n";
	r += "Content-Type: " + type + "\r\n";
	r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	r += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n";
	r += extra;
	r += "\r\n";
	r += body;
	s->write(r);
	s->disconnectFromHost();
}

void DashboardServer::push_audio(int user, const QByteArray &pcm)
{
	auto it = listeners_.find(user);
	if (it == listeners_.end()) return;
	for (QTcpSocket *s : *it) {
		if (s->bytesToWrite() > 64000) continue;	// 느린 청취자(약 4초 밀림)는 건너뜀 → 서버 메모리 보호
		s->write(pcm);
	}
}

int DashboardServer::listener_count() const
{
	int n = 0;
	for (const auto &l : listeners_) n += l.size();
	return n;
}
