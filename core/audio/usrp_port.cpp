// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "usrp_port.h"

#include <cstring>
#include <QJsonDocument>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

UsrpPort::UsrpPort(QObject *parent) : QObject(parent) {}

UsrpPort::~UsrpPort()
{
	if (sending_) end_voice();
}

bool UsrpPort::open(quint16 port)
{
	if (!sock_) {
		sock_ = new QUdpSocket(this);
		connect(sock_, &QUdpSocket::readyRead, this, &UsrpPort::on_ready_read);
		watchdog_ = new QTimer(this);
		connect(watchdog_, &QTimer::timeout, this, &UsrpPort::on_watchdog);
		watchdog_->start(200);
	}
	local_port_ = port;
	const bool ok = sock_->bind(QHostAddress::AnyIPv4, port);
	emit log(ok ? QString("USRP: UDP %1 대기").arg(port) : QString("USRP: 포트 %1 열기 실패 - %2").arg(port).arg(sock_->errorString()));
	return ok;
}

void UsrpPort::on_ready_read()
{
	while (sock_->hasPendingDatagrams()) {
		QByteArray d;
		d.resize(int(sock_->pendingDatagramSize()));
		QHostAddress from;
		quint16 fport = 0;
		sock_->readDatagram(d.data(), d.size(), &from, &fport);
		if (d.size() < 32 || std::memcmp(d.constData(), "USRP", 4) != 0) continue;
		if (!fixed_peer_ && (from != peer_ || fport != peer_port_)) {
			peer_ = from; peer_port_ = fport;
			type_le_ = type_known_ = pyuc_ = false;		// 새 클라이언트: 바이트 순서·종류 다시 알아냄
			emit log(QString("USRP: 클라이언트 %1:%2").arg(from.toString()).arg(fport));
			emit client_changed(QString("%1:%2").arg(from.toString()).arg(fport));
		}
		last_rx_.start();
		const uchar *h = reinterpret_cast<const uchar *>(d.constData());
		const quint32 keyup = qFromBigEndian<quint32>(h + 12);
		quint32 type = qFromBigEndian<quint32>(h + 20);
		if (type > 0xFFFF && qbswap(type) <= 0xFF) {
			type = qbswap(type);
			if (!type_le_) { type_le_ = true; emit log("USRP: type 리틀엔디안 클라이언트(DVSwitch Mobile 방식) → 같은 순서로 응답"); }
		}
		if (type != T_VOICE && type <= 0xFF) type_known_ = true;	// 음성(type 0)은 어느 순서로 읽어도 같아 구분 못 함
		if (type == T_VOICE) {
			if (keyup && d.size() >= 32 + kSamples * 2) {
				if (!ptt_ && !tot_latched_) set_ptt(true, "클라이언트 PTT");
				if (ptt_) {
					for (int i = 0; i < kSamples; ++i) rxq_.enqueue(qFromLittleEndian<qint16>(h + 32 + i * 2));
					while (rxq_.size() > kSamples * 50) rxq_.dequeue();
				}
			}
			else if (!keyup) {
				tot_latched_ = false;
				if (ptt_) set_ptt(false, "클라이언트 PTT 해제");
			}
		}
		else {
			handle_control(type, d.mid(32));
		}
	}
}

static QByteArray c_str(const QByteArray &b)
{
	const int z = b.indexOf('\0');
	return (z >= 0 ? b.left(z) : b).trimmed();
}

// pyUC 계열에 보낼 글자: ASCII만 (알려진 한글 이름표는 영어로, 나머지 ASCII 아닌 글자는 뺌)
static QByteArray to_ascii(const QString &s)
{
	QString t = s;
	t.replace(QString::fromUtf8("브릿지 공지 (AI 음성)"), "BRIDGE NOTICE (AI)");
	t.replace(QString::fromUtf8("브릿지 공지"), "BRIDGE NOTICE");
	QByteArray out;
	for (const QChar c : t) if (c.unicode() >= 0x20 && c.unicode() < 0x7F) out.append(char(c.unicode()));
	return out.simplified();
}

void UsrpPort::handle_control(quint32 type, const QByteArray &pl)
{
	if (type == T_PING) { send_raw(T_PING, false, "PING"); return; }
	if (type == T_TEXT) {
		if (!pl.isEmpty() && uchar(pl[0]) == kTlvSetInfo) {	// 클라이언트 자기 정보 [8][len][DMR ID 3][중계기 4][TG 3][TS][CC][콜사인\0] (pyUC 계열)
			if (pl.size() >= 14) {
				const quint32 id = (quint32(uchar(pl[2])) << 16) | (quint32(uchar(pl[3])) << 8) | uchar(pl[4]);
				const QString call = QString::fromLatin1(c_str(pl.mid(14))).trimmed();
				emit log(QString("USRP: 클라이언트 자기 정보 %1 (%2)").arg(call.isEmpty() ? "-" : call).arg(id));
				QJsonObject o{{"call", call}, {"dmr_id", qint64(id)}, {"app", "USRP Client"}};
				emit client_info(o);
			}
			return;
		}
		const QByteArray t = c_str(pl);
		emit log("USRP: TEXT " + QString::fromLatin1(t));
		if (t.startsWith("CLIENT:{")) {
			const QJsonObject o = QJsonDocument::fromJson(QString::fromUtf8(c_str(pl)).mid(7).toUtf8()).object();
			if (!o.isEmpty()) emit client_info(o);
			return;
		}
		if (t.startsWith("REG:DVSWITCH")) { send_raw(T_TEXT, false, QByteArray("REG:OK", 7)); emit info_requested(); }
		else if (t.startsWith("INFO:")) {
			// DVSwitch Mobile은 INFO:를 요청하지 않음 → 리틀엔디안 클라이언트가 요청하면 pyUC 계열 (INFO json을 받을 수 있음)
			if (type_le_ && !pyuc_) { pyuc_ = true; emit log("USRP: pyUC 계열 클라이언트(USRP Client) → 정보는 ASCII로 보냄"); }
			emit info_requested();
		}
		return;
	}
	if (type == T_DTMF) {
		const QString s = QString::fromLatin1(c_str(pl));
		emit log("USRP: DTMF " + s);
		// "*DMR" 등 모드 명령: 응답하지 않음. INFO JSON으로 응답하면 DVSwitch Mobile이 강제 종료됨 (2026-11-14 실기 확인)
		if (s.startsWith('*')) {
			if (pyuc_ && s.compare("*DMR", Qt::CaseInsensitive) != 0) send_message("DMR only - this bridge supports DMR");
			return;
		}
		apply_tg(s);
		return;
	}
	if (type == T_TLV && pl.size() >= 2 && uchar(pl[0]) == kTlvRemoteCmd) {
		const QString cmd = QString::fromLatin1(c_str(pl.mid(2, qMin(int(uchar(pl[1])), pl.size() - 2))));
		emit log("USRP: 명령 " + cmd);
		if (cmd.startsWith("txTg=")) { if (cmd.mid(5).trimmed() != "0") apply_tg(cmd.mid(5)); }
		else if (cmd.startsWith("tgs=") && !cmd.contains(',')) apply_tg(cmd.mid(4));
		else if (cmd.startsWith("txTs=")) { const int ts = cmd.mid(5).toInt(); if (ts == 1 || ts == 2) emit slot_requested(ts); }
		else if (pyuc_ && (cmd.startsWith("ambeMode=") || cmd.startsWith("ambeSize="))) {
			if (!cmd.mid(9).startsWith("DMR", Qt::CaseInsensitive) && !cmd.startsWith("ambeSize=")) send_message("DMR only - this bridge supports DMR");
		}
	}
}

void UsrpPort::apply_tg(QString s)
{
	s = s.trimmed();
	if (s.startsWith('#')) s.remove(0, 1);
	const bool priv = s.endsWith('#');
	if (priv) s.chop(1);
	bool ok = false;
	const int tg = s.toInt(&ok);
	if (!ok || tg <= 0 || tg > 16777215) { emit log("USRP: TG 해석 실패 " + s); return; }
	if (ptt_) { emit log("USRP: 송신 중이라 TG 변경 무시"); return; }
	// pyUC는 같은 채널 변경을 DTMF와 원격 명령(tgs=)으로 두 번 보냄 → 2초 안의 같은 번호는 한 번만
	if (tg == last_req_tg_ && last_req_.isValid() && last_req_.elapsed() < 2000) { emit log(QString("USRP: 같은 채널 변경(%1) 겹침 - 무시").arg(tg)); return; }
	last_req_tg_ = tg; last_req_.start();
	emit tg_requested(tg, priv);
}

void UsrpPort::on_watchdog()
{
	if (pyuc_ && has_client() && (!keepalive_.isValid() || keepalive_.elapsed() > 5000)) {
		keepalive_.start();
		send_raw(T_PING, false, "PING");
	}
	if (!ptt_) return;
	if (last_rx_.isValid() && last_rx_.elapsed() > 1000) set_ptt(false, "클라이언트 패킷 1초 이상 끊김");
	else if (ptt_clock_.elapsed() > tot_ms_) { tot_latched_ = true; set_ptt(false, "최대 송신 시간 초과 - PTT를 놓았다가 다시 눌러야 송신"); }
}

void UsrpPort::set_ptt(bool keyed, const QString &why)
{
	if (keyed) { ptt_clock_.start(); rxq_.clear(); }
	ptt_ = keyed;
	emit log(QString("USRP: PTT %1 - %2").arg(keyed ? "ON" : "OFF", why));
	emit ptt_changed(keyed);
}

bool UsrpPort::read_frame(int16_t pcm[kSamples])
{
	if (rxq_.size() < kSamples) return false;
	for (int i = 0; i < kSamples; ++i) pcm[i] = rxq_.dequeue();
	return true;
}

void UsrpPort::send_voice(const int16_t pcm[kSamples])
{
	QByteArray a(kSamples * 2, '\0');
	for (int i = 0; i < kSamples; ++i) qToLittleEndian<qint16>(pcm[i], reinterpret_cast<uchar *>(a.data()) + i * 2);
	send_raw(T_VOICE, true, a);
	sending_ = true;
}

void UsrpPort::end_voice()
{
	if (!sending_) return;
	send_raw(T_VOICE, false, QByteArray());
	sending_ = false;
}

void UsrpPort::send_call_info(quint32 src, quint32 rptr, quint32 dst, int slot, int cc, bool priv, const QString &call)
{
	auto tlv = [&](QByteArray c) {
		if (c.isEmpty()) c = QByteArray::number(src);
		QByteArray t;
		t.append(char(kTlvSetInfo));
		t.append(char(3 + 4 + 3 + 1 + 1 + c.size() + 1));
		t.append(char(src >> 16)); t.append(char(src >> 8)); t.append(char(src));
		t.append(char(rptr >> 24)); t.append(char(rptr >> 16)); t.append(char(rptr >> 8)); t.append(char(rptr));
		t.append(char(dst >> 16)); t.append(char(dst >> 8)); t.append(char(dst));
		t.append(char(slot));
		t.append(char((cc & 0x7F) | (priv ? 0x80 : 0)));
		t.append(c);
		t.append('\0');
		return t;
	};
	const QByteArray t = tlv(pyuc_ ? to_ascii(call.trimmed()) : call.trimmed().toUtf8());
	if (type_known_ || type_le_) send_raw(T_TEXT, false, t);
	else {		// 아직 모름 (휴대폰이 음성만 보냄): 각 앱은 자기 순서의 것만 읽음. DVSwitch Mobile용(리틀엔디안)을 마지막에
		send_raw(T_TEXT, false, t, 1);
		send_raw(T_TEXT, false, t, 2);
	}
}

void UsrpPort::send_info(int tg, bool priv, int ts)
{
	// DVSwitch Mobile(type 리틀엔디안 클라이언트)은 INFO JSON 안의 "AB"(Analog_Bridge 정보) 항목을 필수로 읽다가
	// 없으면 강제 종료됨 (2026-11-14 logcat: NullPointerException ServerInfo$AB.toString). 형식이 공개돼 있지 않아 보내지 않음
	if (type_le_ && !pyuc_) return;		// pyUC 계열은 tlv.ambe_mode·last_tune을 읽음 (아래 둘 다 꼭 넣음)
	QString json = QString("INFO:{\"tlv\":{\"ambe_mode\":\"DMR\",\"ts\":%1},\"last_tune\":\"%2%3\"").arg(ts).arg(tg).arg(priv ? "#" : "");
	if (intercom_tg_ > 0) json += QString(",\"intercom_tg\":%1").arg(intercom_tg_);	// 내부 채널 번호 (DVBridge 클라이언트 빠른 버튼)
	json += "}";
	QByteArray b = json.toLatin1();
	b.append('\0');
	send_raw(T_TEXT, false, b);
}

void UsrpPort::send_message(const QString &text)
{
	if (type_le_ && !pyuc_) return;	// DVSwitch Mobile: 같은 텍스트 처리 경로에서 종료될 위험 → 보내지 않음
	QByteArray b = pyuc_ ? "INFO:MSG:" + to_ascii(text) : ("INFO:MSG:" + text).toUtf8();
	b.append('\0');
	send_raw(T_TEXT, false, b);
}

void UsrpPort::send_call_detail(const QJsonObject &o)
{
	// DVSwitch Mobile·아직 모르는 클라이언트(음성만 받음)에는 보내지 않음 (모르는 텍스트에서 종료될 위험). 자체 클라이언트는 CLIENT:·PING으로 알려짐
	if (type_le_ || !type_known_) return;
	QByteArray b = "INFO:CALL:" + QJsonDocument(o).toJson(QJsonDocument::Compact);
	b.append(char(0));
	send_raw(T_TEXT, false, b);
}

void UsrpPort::send_raw(quint32 type, bool keyup, const QByteArray &payload, int order)
{
	if (!sock_ || !has_client()) return;
	QByteArray d(32, '\0');
	uchar *h = reinterpret_cast<uchar *>(d.data());
	std::memcpy(h, "USRP", 4);
	qToBigEndian<quint32>(seq_++, h + 4);
	qToBigEndian<quint32>(keyup ? 1 : 0, h + 12);
	if (order == 2 || (order == 0 && type_le_)) qToLittleEndian<quint32>(type, h + 20); else qToBigEndian<quint32>(type, h + 20);
	d.append(payload);
	sock_->writeDatagram(d, peer_, peer_port_);
}
