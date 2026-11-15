// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// Homebrew(MMDVM) 마스터 접속 클라이언트 — 공개된 Homebrew Repeater 프로토콜 형식 기준으로 새로 작성
//   RPTL+ID → RPTACK+salt → RPTK+ID+SHA256(salt+비밀번호) → RPTACK → RPTC 설정(302바이트) → RPTACK → 접속
//   (옵션이 있으면 RPTO+ID+문자열 송신, 응답은 기다리지 않음)
//   유지: RPTPING ↔ MSTPONG, 종료: RPTCL, 거부: MSTNAK, 서버 종료: MSTCL
//
// BM 차단 방지:
//   - 실패(무응답·거부)마다 재시도 대기 5초 → 10 → 20 … 최대 300초, 접속 성공하면 초기화
//   - stop() 시 RPTCL 송신, 자동 재접속 끔
#pragma once
#include <QObject>
#include <QHostAddress>
#include <QString>
#include <QElapsedTimer>
#include "dmr/voice.h"

class QUdpSocket;
class QTimer;

struct HomebrewConfig {
	QString host = "4501.master.brandmeister.network";
	quint16 port = 62031;
	quint32 id = 0;				// 핫스팟 ID (7자리 DMR ID 또는 9자리 = ID×100+접미사)
	QString password;
	QString callsign;
	quint32 rx_freq = 438800000, tx_freq = 438800000;
	quint8 power = 1, cc = 1;
	double latitude = 0.0, longitude = 0.0;	// RPTC에는 공백 없는 고정폭 숫자로 기록
	quint16 height = 0;
	QString location = "Windows", description = "DMRBridge";
	char slot_mode = '4';			// '4' = 단일 슬롯 핫스팟(DMO)
	QString url;
	// BM은 패키지 ID를 "MMDVM_<하드웨어>" 형식으로 해석 (BM 장치 정보 표시로 확인), 소프트웨어 ID는 날짜 형식이 일반적
	QString software_id = "20261114";
	QString package_id = "MMDVM_DMRBridge";
	QString options;			// 예: "TS2=450" (선택)
};

class HomebrewClient : public QObject
{
	Q_OBJECT
public:
	enum State { Stopped, Resolving, WaitLogin, WaitAuth, WaitConfig, Connected, Backoff };
	Q_ENUM(State)

	explicit HomebrewClient(QObject *parent = nullptr);
	~HomebrewClient() override;

	void start(const HomebrewConfig &cfg);
	void stop();				// RPTCL 보내고 정지
	State state() const { return state_; }
	bool connected() const { return state_ == Connected; }
	void send(const dmr::Dmrd &p);
	const HomebrewConfig &config() const { return cfg_; }
	void set_description(const QString &d) { cfg_.description = d; }	// 다음 로그인(재시도 포함)부터 RPTC 설명 칸에 반영
	void set_location(const QString &l) { cfg_.location = l; }		// 다음 로그인부터 RPTC 위치 칸 (BM은 이 칸을 보여 줌, 설명 칸은 안 씀)
	int next_backoff_seconds() const { return backoff_s_; }

	static QByteArray build_config_packet(const HomebrewConfig &cfg);	// RPTC 302바이트 (시험용 공개)

signals:
	void state_changed(HomebrewClient::State s);
	void log(QString text);
	void dmrd_received(dmr::Dmrd p);

private slots:
	void on_ready_read();
	void on_tick();

private:
	void set_state(State s);
	void begin_login();
	void fail(const QString &why);
	void send_raw(const QByteArray &d);
	QByteArray id_bytes() const;

	HomebrewConfig cfg_;
	QUdpSocket *sock_ = nullptr;
	QTimer *tick_ = nullptr;
	QHostAddress addr_;
	State state_ = Stopped;
	QElapsedTimer step_clock_;		// 현재 단계 시작 시각
	QElapsedTimer last_pong_;
	QElapsedTimer last_ping_;
	int backoff_s_ = 5;
	qint64 backoff_until_ = 0;
	QElapsedTimer mono_;
	int rx_dmrd_ = 0, rx_pong_ = 0, rx_other_ = 0;	// 진단용 수신 집계 (stop 때 기록)
};
