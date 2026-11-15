// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// UsrpPort — USRP 클라이언트(DVSwitch Mobile, pyUC 등)와 음성·제어를 주고받음
//   패킷: 헤더 32바이트(빅엔디안) "USRP" seq memory keyup talkgroup type mpxid reserved + 음성 160샘플(리틀엔디안)
//   ※ DVSwitch Mobile(안드로이드)은 type 칸만 리틀엔디안으로 보냄 → 자동 인식, 같은 순서로 응답 (2026-11-14 실기 확인)
//   제어(공개 형식 기준 자체 작성): TEXT "REG:DVSWITCH"→"REG:OK", "INFO:"→"INFO:{json}", DTMF "450"/"450#"/"#450"/"*DMR",
//         TLV REMOTE_CMD "txTg=…" "txTs=…", PING, 수신 시작 시 SET_INFO
//         DMRBridge 클라이언트 전용(type 빅엔디안): TEXT "CLIENT:{json}" 내 정보 수신, "INFO:CALL:{json}" 상대방 정보 송신
//   앱 설정 배치: 앱 "TX Port"(기본 32001) = 이 포트, 앱 "Port"(34001) = 앱이 받는 포트(첫 패킷 보낸 곳으로 자동 응답)
//
// 송신 안전장치: TOT 초과 → PTT 해제 + 클라이언트가 PTT를 놓을 때까지 재송신 금지 / 패킷 1초 끊김 → PTT 해제
#pragma once
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>

class QUdpSocket;
class QTimer;

class UsrpPort : public QObject
{
	Q_OBJECT
public:
	enum { T_VOICE = 0, T_DTMF = 1, T_TEXT = 2, T_PING = 3, T_TLV = 4 };
	static constexpr uchar kTlvRemoteCmd = 5;
	static constexpr uchar kTlvSetInfo = 8;
	static constexpr int kSamples = 160;

	explicit UsrpPort(QObject *parent = nullptr);
	~UsrpPort() override;

	bool open(quint16 local_port);					// 0.0.0.0:local_port
	void set_fixed_peer(const QHostAddress &a, quint16 p) { peer_ = a; peer_port_ = p; fixed_peer_ = true; }
	void set_tot_seconds(int s) { tot_ms_ = qMax(1, s) * 1000; }
	void set_intercom_tg(int tg) { intercom_tg_ = tg; }	// INFO json에 알려 줄 내부 채널 번호 (0 = 없음)
	quint16 local_port() const { return local_port_; }
	bool ptt() const { return ptt_; }
	bool has_client() const { return !peer_.isNull() && peer_port_; }
	bool type_le() const { return type_le_; }		// true = DVSwitch Mobile
	bool type_known() const { return type_known_; }	// 음성 말고 다른 패킷(PING·REG·CLIENT 등)을 받아 바이트 순서를 앎

	// 클라이언트 → 본체: PTT 중 쌓인 PCM 꺼내기 (160샘플 단위)
	bool read_frame(int16_t pcm[kSamples]);
	int queued_frames() const { return rxq_.size() / kSamples; }

	// 본체 → 클라이언트
	void send_voice(const int16_t pcm[kSamples]);
	void end_voice();								// keyup=0
	// 이름은 UTF-8 (DVSwitch Mobile도 한글 표시 - 2026-11-15 실기 확인). 아직 바이트 순서를 모르면(음성만 받음) 빅엔디안 → 리틀엔디안 순서로 둘 다
	//   (DVSwitch Mobile은 리틀엔디안 SET_INFO 뒤에 빅엔디안 것이 오면 이름을 표시하지 않음 → 리틀엔디안을 마지막에)
	void send_call_info(quint32 src, quint32 rptr, quint32 dst, int slot, int cc, bool priv, const QString &call);
	void send_info(int tg, bool priv, int ts);		// INFO json
	void send_message(const QString &text);			// INFO:MSG:… (앱 알림)
	void send_call_detail(const QJsonObject &o);	// INFO:CALL:{json} 상대방 정보 (DMRBridge 클라이언트만)

signals:
	void ptt_changed(bool keyed);
	void tg_requested(int tg, bool priv);
	void slot_requested(int ts);					// 1 또는 2
	void info_requested();
	void client_info(QJsonObject info);			// 클라이언트가 보낸 내 정보 (콜사인·DMR ID·이름·장비명·앱)
	void client_changed(QString address);			// 새 클라이언트 주소:포트
	void log(QString text);

private slots:
	void on_ready_read();
	void on_watchdog();

private:
	void handle_control(quint32 type, const QByteArray &payload);
	void apply_tg(QString s);
	void set_ptt(bool keyed, const QString &why);
	void send_raw(quint32 type, bool keyup, const QByteArray &payload, int order = 0);	// order: 0 = 클라이언트 순서, 1 = 빅엔디안, 2 = 리틀엔디안

	QUdpSocket *sock_ = nullptr;
	QTimer *watchdog_ = nullptr;
	quint16 local_port_ = 0;
	QHostAddress peer_;
	quint16 peer_port_ = 0;
	bool fixed_peer_ = false;
	bool type_le_ = false;
	bool type_known_ = false;
	int intercom_tg_ = 0;
	quint32 seq_ = 0;

	QQueue<int16_t> rxq_;
	bool ptt_ = false;
	bool tot_latched_ = false;
	int tot_ms_ = 180000;
	QElapsedTimer ptt_clock_, last_rx_;
	bool sending_ = false;
	QSet<quint64> seen_;
};
