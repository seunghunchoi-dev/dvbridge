// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "homebrew_client.h"

#include <QCryptographicHash>
#include <QHostInfo>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

namespace {
constexpr int kStepTimeoutMs = 5000;	// 로그인 각 단계 응답 대기
constexpr int kPingMs = 5000;			// 핑 간격
constexpr int kPongTimeoutMs = 30000;	// 이 시간 동안 퐁이 없으면 끊긴 것으로 봄
constexpr int kBackoffMaxS = 300;

QByteArray fixed(const QString &s, int n, bool right_align = false)
{
	QByteArray b = s.toLatin1().left(n);
	const QByteArray pad(n - b.size(), ' ');
	return right_align ? pad + b : b + pad;
}
}

HomebrewClient::HomebrewClient(QObject *parent) : QObject(parent)
{
	mono_.start();
	tick_ = new QTimer(this);
	connect(tick_, &QTimer::timeout, this, &HomebrewClient::on_tick);
}

HomebrewClient::~HomebrewClient()
{
	stop();
}

QByteArray HomebrewClient::id_bytes() const
{
	QByteArray b(4, '\0');
	qToBigEndian<quint32>(cfg_.id, reinterpret_cast<uchar *>(b.data()));
	return b;
}

QByteArray HomebrewClient::build_config_packet(const HomebrewConfig &c)
{
	QByteArray d("RPTC");
	QByteArray id(4, '\0');
	qToBigEndian<quint32>(c.id, reinterpret_cast<uchar *>(id.data()));
	d += id;
	d += fixed(c.callsign, 8);
	d += QString("%1").arg(c.rx_freq, 9, 10, QChar('0')).toLatin1();
	d += QString("%1").arg(c.tx_freq, 9, 10, QChar('0')).toLatin1();
	d += QString("%1").arg(c.power, 2, 10, QChar('0')).toLatin1();
	d += QString("%1").arg(c.cc, 2, 10, QChar('0')).toLatin1();
	// 위도 8자·경도 9자: 공백 없이 0으로 채운 숫자 (공백이 섞이면 BM이 RPTC를 MSTNAK로 거부함, 2026-11-14 확인)
	d += QString("%1").arg(c.latitude, 8, 'f', 4, QChar('0')).toLatin1().left(8);
	d += QString("%1").arg(c.longitude, 9, 'f', 4, QChar('0')).toLatin1().left(9);
	d += QString("%1").arg(c.height, 3, 10, QChar('0')).toLatin1();
	d += fixed(c.location, 20);
	d += fixed(c.description, 19);
	d += QByteArray(1, c.slot_mode);
	d += fixed(c.url, 124);
	d += fixed(c.software_id, 40);
	d += fixed(c.package_id, 40);
	return d;	// 4+4+8+9+9+2+2+8+9+3+20+19+1+124+40+40 = 302
}

void HomebrewClient::start(const HomebrewConfig &cfg)
{
	cfg_ = cfg;
	if (!sock_) {
		sock_ = new QUdpSocket(this);
		sock_->bind(QHostAddress::AnyIPv4, 0);
		connect(sock_, &QUdpSocket::readyRead, this, &HomebrewClient::on_ready_read);
	}
	backoff_s_ = 5;
	tick_->start(250);
	begin_login();
}

void HomebrewClient::begin_login()
{
	set_state(Resolving);
	QHostAddress direct(cfg_.host);
	if (!direct.isNull()) addr_ = direct;
	else {
		const QHostInfo hi = QHostInfo::fromName(cfg_.host);	// 짧게 막힘 (접속 시점에만)
		addr_ = QHostAddress();
		for (const QHostAddress &a : hi.addresses()) {
			if (a.protocol() == QAbstractSocket::IPv4Protocol) { addr_ = a; break; }
		}
		if (addr_.isNull()) { fail("주소를 찾지 못함: " + cfg_.host); return; }
	}
	emit log(QString("마스터 %1 (%2:%3) 로그인 시작, ID %4").arg(cfg_.host, addr_.toString()).arg(cfg_.port).arg(cfg_.id));
	set_state(WaitLogin);
	step_clock_.start();
	send_raw("RPTL" + id_bytes());
}

void HomebrewClient::stop()
{
	if (state_ == Connected || state_ == WaitConfig || state_ == WaitAuth) {
		send_raw("RPTCL" + id_bytes());
		emit log(QString("접속 해제 (RPTCL) - 받은 패킷: DMRD %1, 퐁 %2, 기타 %3").arg(rx_dmrd_).arg(rx_pong_).arg(rx_other_));
	}
	if (tick_) tick_->stop();
	set_state(Stopped);
}

void HomebrewClient::send(const dmr::Dmrd &p)
{
	if (state_ != Connected) return;
	const std::vector<uint8_t> d = p.pack();
	send_raw(QByteArray(reinterpret_cast<const char *>(d.data()), int(d.size())));
}

void HomebrewClient::send_raw(const QByteArray &d)
{
	if (sock_ && !addr_.isNull()) sock_->writeDatagram(d, addr_, cfg_.port);
}

void HomebrewClient::set_state(State s)
{
	if (s == state_) return;
	static const char *names[] = {"정지", "주소 확인", "로그인 대기", "인증 대기", "설정 대기", "접속됨", "재시도 대기"};
	emit log(QString("상태: %1 → %2").arg(names[state_]).arg(names[s]));
	state_ = s;
	emit state_changed(s);
}

void HomebrewClient::fail(const QString &why)
{
	backoff_until_ = mono_.elapsed() + qint64(backoff_s_) * 1000;
	emit log(QString("%1 → %2초 뒤 재시도").arg(why).arg(backoff_s_));
	backoff_s_ = qMin(backoff_s_ * 2, kBackoffMaxS);
	set_state(Backoff);
}

void HomebrewClient::on_tick()
{
	switch (state_) {
	case WaitLogin: case WaitAuth: case WaitConfig:
		if (step_clock_.elapsed() > kStepTimeoutMs) fail("마스터 응답 없음");
		break;
	case Connected:
		if (last_ping_.elapsed() > kPingMs) { send_raw("RPTPING" + id_bytes()); last_ping_.start(); }
		if (last_pong_.elapsed() > kPongTimeoutMs) fail("핑 응답이 30초 동안 없음");
		break;
	case Backoff:
		if (mono_.elapsed() >= backoff_until_) begin_login();
		break;
	default:
		break;
	}
}

void HomebrewClient::on_ready_read()
{
	while (sock_->hasPendingDatagrams()) {
		QByteArray d;
		d.resize(int(sock_->pendingDatagramSize()));
		QHostAddress from;
		sock_->readDatagram(d.data(), d.size(), &from);
		if (from.toIPv4Address() != addr_.toIPv4Address()) continue;	// 마스터가 아닌 곳에서 온 것은 무시

		if (d.startsWith("DMRD")) {
			++rx_dmrd_;
			dmr::Dmrd p;
			if (state_ == Connected && dmr::Dmrd::unpack(reinterpret_cast<const uint8_t *>(d.constData()), d.size(), p)) emit dmrd_received(p);
			continue;
		}
		if (d.startsWith("MSTPONG")) { ++rx_pong_; last_pong_.start(); continue; }
		if (!d.startsWith("RPTACK") && !d.startsWith("MSTNAK") && !d.startsWith("MSTCL")) {
			++rx_other_;
			emit log("기타 패킷 수신: " + QString::fromLatin1(d.left(8).toHex(' ')));
			continue;
		}
		if (d.startsWith("MSTNAK")) {
			if (state_ == Connected) fail("마스터가 연결을 거부(MSTNAK)");
			else fail(state_ == WaitAuth ? "로그인 거부(MSTNAK) - 비밀번호를 확인하세요" : "로그인 거부(MSTNAK)");
			continue;
		}
		if (d.startsWith("MSTCL")) { fail("마스터가 연결을 닫음(MSTCL)"); continue; }
		if (!d.startsWith("RPTACK")) continue;

		if (state_ == WaitLogin && d.size() >= 10) {
			const QByteArray salt = d.mid(6, 4);
			const QByteArray hash = QCryptographicHash::hash(salt + cfg_.password.toUtf8(), QCryptographicHash::Sha256);
			set_state(WaitAuth);
			step_clock_.start();
			send_raw("RPTK" + id_bytes() + hash);
		}
		else if (state_ == WaitAuth) {
			set_state(WaitConfig);
			step_clock_.start();
			send_raw(build_config_packet(cfg_));
		}
		else if (state_ == WaitConfig) {
			if (!cfg_.options.isEmpty()) send_raw("RPTO" + id_bytes() + cfg_.options.toLatin1());
			backoff_s_ = 5;
			last_pong_.start();
			last_ping_.start();
			emit log("마스터 접속 완료");
			set_state(Connected);
		}
	}
}
