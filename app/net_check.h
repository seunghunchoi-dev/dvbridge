// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// NetCheck — 내부망·외부망 연결 점검 (읽기만 함: 방화벽·네트워크 설정은 바꾸지 않음)
//   - 이 PC의 주소: 사설(내부망)/공인(외부망) 구분, 인터넷으로 나가는 쪽 주소 → 공유기 뒤(NAT)인지
//   - 열린 포트(웹, USRP), Windows 방화벽 상태·DVBridge 허용 규칙, 네트워크 종류(공용/개인)
//   - 인터넷: BM 마스터 이름 확인(DNS), RadioID 접속, (선택) 외부 서비스로 공인 IP 조회
//   - 결과에 맞는 해야 할 일 안내
#pragma once
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QNetworkAccessManager;
class QTimer;

namespace NetKind {
// 주소 종류: "내부망"(사설·공유 IP), "외부망"(공인), "링크로컬", "이 PC"
QString of(const QHostAddress &a);
QString of_text(const QString &addr_or_addr_port);	// "192.168.0.10:32002" 같은 문자열도 됨
bool is_public(const QHostAddress &a);
}

class NetCheck : public QObject
{
	Q_OBJECT
public:
	struct Input {
		QString web_bind;
		quint16 web_port = 0;
		bool web_listening = false;
		bool admin_password = false;
		QString exe_path;
		QStringList master_hosts;			// 사용자들의 마스터 주소 (중복 제거)
		QJsonArray users;					// {call, usrp_port, running, connected, client}
	};
	explicit NetCheck(QObject *parent = nullptr);
	void start(const Input &in, bool lookup_public_ip);
	bool running() const { return pending_ > 0; }
	QJsonObject result() const { return res_; }

signals:
	void finished();

private:
	void done_one();
	void finish();
	void build_advice();

	Input in_;
	QJsonObject res_;
	int pending_ = 0;
	QNetworkAccessManager *nam_ = nullptr;
	QTimer *timeout_ = nullptr;
	QString outbound_ip_, public_ip_;
};
