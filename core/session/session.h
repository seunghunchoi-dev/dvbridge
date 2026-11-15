// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// Session — 사용자 한 명분 브릿지: USRP 클라이언트 ⇄ 보코더 ⇄ DMR 음성 ⇄ Homebrew 마스터
//
// 송신 안전장치 (BM 차단 방지)
//   - 마스터 미접속이면 송신 안 함
//   - BM Loop Prevention 대비: "2초 미만 송신이 60초 안에 6회 이상 → 해당 마스터에서 DMR ID 1시간 차단"(BM 도움말)
//     → 최근 60초 안에 2초 미만 송신이 이미 5회(short_tx_limit) 있으면 다음 송신(6번째 키업)을 막음, 5회 이하로 내려가면 다시 허용
//   - TOT·패킷 끊김은 UsrpPort가 처리
#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <memory>
#include "net/homebrew_client.h"
#include "dmr/voice.h"
#include "call_log.h"
#include "id_db.h"

class UsrpPort;
class VocoderHost;
class QTimer;

struct SessionConfig {
	QString name;				// 표시용 (예: 콜사인)
	HomebrewConfig master;
	quint32 dmr_id = 0;			// 송신자 ID (7자리)
	int tg = 9990;				// 기본 대상
	bool priv = true;			// 기본: 9990 개인호출(Parrot)
	int slot = 2;
	int cc = 1;
	quint16 usrp_port = 32001;
	int tot_seconds = 180;
	int short_tx_limit = 5;		// BM 규칙: 60초 안 2초 미만 키업이 5회를 넘으면(6회째) 차단 → 5회까지만 허용
	// BM Unlink: TG 4000 그룹호출 = 동적 TG 연결 해제. 그룹 TG로 송신한 뒤 다른 대상으로 바꿀 때 자동으로 먼저 보냄
	bool auto_unlink = true;
	int unlink_tg = 4000;
	int unlink_ms = 2200;		// 무음 길이. 2초 이상이라 BM 루프 방지의 "2초 미만" 횟수에 들어가지 않음
	QString plugin_dir;
	bool enabled = true;		// 다중 사용자: 전체 시작 때 이 사용자를 켤지
	// 마지막으로 붙은 클라이언트의 장비명 (예: "Galaxy A17") → BM 위치 칸 "DMRBridge Galaxy A17" (make_location)
	QString device;
	// 장비명이 바뀌어 BM에 다시 접속하는 것은 이 간격(초)에 한 번까지 (첫 반영은 바로). 시험에서 줄여 쓸 수 있음
	int device_relogin_min_s = 600;
	// 내부 채널: 이 번호로 말하면 BM으로 보내지 않고 같은 번호를 고른 이 브릿지의 다른 사용자에게만 들림 (0 = 끔)
	int intercom_tg = 99;
};

// DMRBridge 클라이언트가 보낸 내 정보 (표시용 - 서버 설정을 덮어쓰지 않음)
struct ClientInfo {
	QString call, name, device, app;
	quint32 dmr_id = 0;
	bool valid() const { return !call.isEmpty() || dmr_id || !device.isEmpty(); }
};

class Session : public QObject
{
	Q_OBJECT
public:
	explicit Session(const SessionConfig &cfg, QObject *parent = nullptr);
	~Session() override;
	bool start();					// USRP 포트 열기 + 보코더 로드 + 마스터 접속
	void stop();
	const SessionConfig &config() const { return cfg_; }
	HomebrewClient *master() const { return hb_; }
	UsrpPort *usrp() const { return usrp_; }
	bool transmitting() const { return tx_.active(); }
	int tx_count() const { return tx_count_; }
	int short_tx_in_window();		// 최근 60초 안의 2초 미만 송신 수 (같은 DMR ID의 모든 세션 합산 - BM은 DMR ID 기준으로 차단)
	int unlink_count() const { return unlink_count_; }
	bool active_call(CallRecord &r) const;		// 지금 진행 중인 송수신 (없으면 false)
	QString master_state_text() const;
	QString client_address() const { return client_addr_; }
	void set_id_db(const IdDatabase *db) { ids_ = db; }	// 콜사인·이름 조회 (없어도 동작)
	bool unlinking() const { return unlinking_; }
	void request_unlink(const QString &why);	// TG 4000 그룹호출(무음) 송신
	// 통화 대상 변경 (휴대폰 번호# 입력과 같은 규칙: Parrot은 개인호출, 4000은 Unlink, 바꾸기 전 자동 Unlink)
	// source: 로그에 붙일 출처 (예: "웹 설정"). 반환: 실패 이유, 성공이면 빈 문자열
	QString set_target(int tg, bool priv, const QString &source);
	const ClientInfo &client_info() const { return client_; }
	// BM RPTC 설명 칸(19자): "DMRBridge" + 장비명(앞의 "Galaxy " 생략, 영문·숫자만), 넘치면 자름
	static QString make_description(const QString &device);
	// BM RPTC 위치 칸(20자, BM이 실제로 표시하는 칸): "DMRBridge " + 장비명(영문·숫자만), 넘치면 "Galaxy " 빼고, 그래도 넘치면 자름
	static QString make_location(const QString &device);
	// 송신 이름표(Talker Alias, 31자): "콜사인 DMRBridge 장비명" (예: "HL0ABC DMRBridge Galaxy A17"), 넘치면 Galaxy 생략 후 자름
	static QString make_talker_alias(const QString &callsign, const QString &device);

	// ---- 브릿지 공지: BM과 무관하게 이 사용자의 휴대폰·클라이언트에만 음성(8kHz 16bit)을 보냄 ----
	//   BM 수신·클라이언트 송신·Unlink 중에는 기다렸다가(BM 우선), 끼어들면 멈췄다가 이어서 보냄
	//   휴대폰 화면 이름표: "브릿지 공지" (DVSwitch Mobile·DMRBridge 클라이언트 모두 한글 표시)
	//   source: "tts"(문장 읽기) | "live"(실시간 마이크) | "schedule"(예약)
	void notice_add(const QString &title, const QString &source, const QVector<int16_t> &pcm8k, bool ai = false);	// ai = AI 음성 모델로 만든 소리
	void notice_live(const QVector<int16_t> &pcm8k, bool end);	// 실시간 마이크 조각 이어 붙이기 (end = 방송 끝)
	void notice_stop();						// 지금 공지와 대기 중인 공지 모두 취소
	bool notice_active() const;			// 공지를 보내는 중 (내부 채널 수신은 빼고)
	int notice_waiting() const;			// 대기 중인 공지 수 (내부 채널 빼고)
	QJsonObject notice_status() const;	// 웹 진행 표시: state(sending·waiting·idle), progress(0~1), total_s, reason
	static QString notice_label(bool ai = false) { return ai ? QStringLiteral("브릿지 공지 (AI 음성)") : QStringLiteral("브릿지 공지"); }

	// ---- 내부 채널 (BM과 무관) ----
	bool on_intercom() const { return cfg_.intercom_tg > 0 && cfg_.tg == cfg_.intercom_tg; }
	void set_intercom_tg(int tg);
	// 다른 사용자가 내부 채널로 말한 소리를 이 사용자의 휴대폰으로 (공지와 같은 대기·BM 우선 규칙). key = 말하는 사람 구분
	void intercom_feed(const QString &key, quint32 src_id, const QString &call, const QString &name, const QVector<int16_t> &pcm8k, bool end);

signals:
	void log(QString text);
	void tx_state(bool on, int tg, bool priv);
	void rx_state(bool on, quint32 src, quint32 dst, bool priv);
	void target_changed(int tg, bool priv);		// 앱에서 TG를 바꿨을 때
	void call_started(CallRecord r);			// 송신·수신·Unlink 시작 (대시보드 실시간 표시용)
	void call_finished(CallRecord r);			// 끝 → 이력 저장
	void audio(QByteArray pcm160, bool tx);
	void device_changed(QString device);		// 클라이언트 장비명이 바뀜 → 설정에 저장
	void client_info_changed();
	void intercom_audio(QVector<int16_t> pcm, bool end);	// 이 사용자가 내부 채널로 말함 (MainWindow가 같은 채널 사용자에게 전달)
	void rx_alias(QString alias);				// 상대방 이름표(Talker Alias) 받음		// 8kHz 16bit 160샘플 (실시간 청취용: 수신=BM에서 온 소리, 송신=휴대폰 소리)

private slots:
	void on_ptt(bool keyed);
	void on_tx_tick();
	void on_dmrd(dmr::Dmrd p);
	void on_rx_watch();
	void on_unlink_tick();
	void on_notice_tick();

private:
	void send_all(const std::vector<dmr::Dmrd> &v);
	void finish_tx(const QString &why);

	SessionConfig cfg_;
	HomebrewClient *hb_ = nullptr;
	UsrpPort *usrp_ = nullptr;
	std::unique_ptr<VocoderHost> voc_;
	QTimer *tx_timer_ = nullptr;
	QTimer *rx_watch_ = nullptr;
	dmr::VoiceTx tx_;
	dmr::VoiceRx rx_;
	QElapsedTimer last_tx_end_, last_rx_pkt_;
	bool ptt_latched_ = false;		// 막힌 PTT → 클라이언트가 PTT를 놓을 때까지 무시
	QElapsedTimer tx_started_;
	QElapsedTimer mono_;
	// Unlink
	QTimer *unlink_timer_ = nullptr;
	std::vector<dmr::Dmrd> unlink_q_;
	size_t unlink_pos_ = 0;
	bool unlinking_ = false;
	int linked_tg_ = 0;				// 이번 접속에서 마지막으로 송신한 그룹 TG (동적 연결이 걸려 있을 수 있음)
	int unlink_count_ = 0;
	// 이력
	CallRecord tx_rec_, rx_rec_;
	bool rx_rec_active_ = false;
	QString client_addr_;
	const IdDatabase *ids_ = nullptr;
	void fill_names(CallRecord &r) const;
	static QElapsedTimer &shared_clock();
	static QList<qint64> &short_list(quint32 dmr_id);
	void finish_rx_record();
	int tx_count_ = 0;
	// 클라이언트 정보·장비명 반영
	ClientInfo client_;
	bool reconfig_pending_ = false;
	QElapsedTimer relogin_clock_;		// 마지막 장비명 재접속 (10분에 1번 제한)
	bool relogin_wait_logged_ = false;
	void on_client_info(const QJsonObject &o);
	void try_reconfig();
	// 브릿지 공지
	struct NoticeClip {
		QString title, source;		// source "intercom" = 내부 채널 (공지 아님)
		QVector<int16_t> pcm;
		bool open = false;			// 실시간 마이크·내부 채널: 뒤에 더 붙음
		QString key, call;			// 내부 채널: 말하는 사람 구분·콜사인
		quint32 src_id = 0;
		bool ai = false;			// AI 음성 모델로 만든 공지 → 이름표에 표시
		QElapsedTimer queued;		// 대기열에 들어온 때 (못 보내고 오래 기다리면 버림)
		QElapsedTimer last_add;		// 실시간 조각이 마지막으로 온 때 (끊기면 닫음)
	};
	QList<NoticeClip> notice_q_;
	int notice_pos_ = 0;			// 첫 클립에서 보낸 샘플 수
	bool notice_on_ = false;		// 지금 클라이언트에 공지 음성을 보내는 중
	bool notice_resume_ = false;	// 끼어든 통화 때문에 멈춘 뒤 이어서 보내는 중
	bool notice_rec_active_ = false;
	CallRecord notice_rec_;
	QTimer *notice_timer_ = nullptr;
	QElapsedTimer notice_clock_, notice_idle_;
	qint64 notice_sent_ = 0;
	QString notice_wait_reason_;		// 지금 못 보내는 이유 (웹 표시)
	void notice_begin();
	bool ic_tx_ = false;			// 이 사용자가 내부 채널로 말하는 중 (BM으로 안 보냄)
	void finish_intercom_tx(const QString &why);
	void notice_pause(const QString &why);
	void notice_finish(const QString &why);
};
