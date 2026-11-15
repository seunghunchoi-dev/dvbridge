// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "session.h"

#include <algorithm>
#include <QDateTime>
#include <QHash>
#include <QRandomGenerator>
#include <QTimer>
#include "audio/usrp_port.h"
#include "vocoder/vocoder_host.h"

Session::Session(const SessionConfig &cfg, QObject *parent) : QObject(parent), cfg_(cfg)
{
	mono_.start();
	cfg_.master.description = make_description(cfg_.device);
	cfg_.master.location = make_location(cfg_.device);
	hb_ = new HomebrewClient(this);
	usrp_ = new UsrpPort(this);
	voc_ = std::make_unique<VocoderHost>();
	tx_timer_ = new QTimer(this);
	rx_watch_ = new QTimer(this);
	unlink_timer_ = new QTimer(this);
	connect(unlink_timer_, &QTimer::timeout, this, &Session::on_unlink_tick);
	connect(hb_, &HomebrewClient::log, this, [this](QString s) { emit log("[마스터] " + s); });
	connect(usrp_, &UsrpPort::log, this, [this](QString s) { emit log("[USRP] " + s); });
	connect(hb_, &HomebrewClient::dmrd_received, this, &Session::on_dmrd);
	connect(usrp_, &UsrpPort::ptt_changed, this, &Session::on_ptt);
	connect(usrp_, &UsrpPort::client_changed, this, [this](QString a) { client_addr_ = a; client_ = ClientInfo(); emit client_info_changed(); });
	connect(usrp_, &UsrpPort::client_info, this, &Session::on_client_info);
	connect(usrp_, &UsrpPort::tg_requested, this, [this](int tg, bool priv) { set_target(tg, priv, QString()); });
	connect(usrp_, &UsrpPort::slot_requested, this, [this](int ts) { cfg_.slot = ts; usrp_->send_info(cfg_.tg, cfg_.priv, cfg_.slot); });
	connect(usrp_, &UsrpPort::info_requested, this, [this] { usrp_->send_info(cfg_.tg, cfg_.priv, cfg_.slot); });
	connect(tx_timer_, &QTimer::timeout, this, &Session::on_tx_tick);
	connect(rx_watch_, &QTimer::timeout, this, &Session::on_rx_watch);
	notice_timer_ = new QTimer(this);
	notice_timer_->setTimerType(Qt::PreciseTimer);
	connect(notice_timer_, &QTimer::timeout, this, &Session::on_notice_tick);
}

Session::~Session() { stop(); }

void Session::set_intercom_tg(int tg)
{
	cfg_.intercom_tg = tg;
	usrp_->set_intercom_tg(tg);
	usrp_->send_info(cfg_.tg, cfg_.priv, cfg_.slot);
}

QString Session::set_target(int tg, bool priv, const QString &source)
{
	if (tg < 1 || tg > 16777215) return QString("잘못된 번호 %1").arg(tg);
	// 웹 등 바깥에서 바꿀 때만 송신 중 거부 (휴대폰 번호# 입력은 예전처럼 그대로 처리)
	if (!source.isEmpty() && (tx_.active() || unlinking_)) return "송신 중에는 바꿀 수 없습니다. 송신이 끝난 뒤 다시 하세요";
	if (!source.isEmpty() && ic_tx_) return "송신 중에는 바꿀 수 없습니다. 송신이 끝난 뒤 다시 하세요";
	const QString from = source.isEmpty() ? QString() : " (" + source + ")";
	if (cfg_.intercom_tg > 0 && tg == cfg_.intercom_tg) {		// 내부 채널: BM으로는 아무것도 보내지 않음
		if (cfg_.auto_unlink && linked_tg_) request_unlink(QString("TG %1 연결 해제 후 내부 채널").arg(linked_tg_));
		cfg_.tg = tg; cfg_.priv = false;
		emit log(QString("대상 변경 → 내부 채널 %1 (BM으로 나가지 않음)%2").arg(tg).arg(from));
		emit target_changed(tg, false);
		usrp_->send_info(cfg_.tg, cfg_.priv, cfg_.slot);
		usrp_->send_message(QString("Intercom %1 (bridge only)").arg(tg));
		return QString();
	}
	// BM Parrot(9990, 국가코드+997 예: 450997)은 개인호출로만 응답 → '#' 없이 보내도 개인호출로 처리
	const bool parrot = (tg == 9990) || (tg >= 100997 && tg <= 999997 && tg % 1000 == 997);
	if (parrot && !priv) { priv = true; emit log(QString("%1(Parrot)은 개인호출로 자동 변경").arg(tg)); }
	if (tg == cfg_.unlink_tg && !priv) {	// 4000 = Unlink 명령. 통화 대상은 그대로
		request_unlink("수동 (4000)" + from);
		return QString();
	}
	if (cfg_.auto_unlink && linked_tg_ && (tg != linked_tg_ || priv)) request_unlink(QString("TG %1 연결 해제 후 변경").arg(linked_tg_));
	cfg_.tg = tg; cfg_.priv = priv;
	emit log(QString("대상 변경 → %1%2%3").arg(tg).arg(priv ? " (개인호출)" : "").arg(from));
	emit target_changed(tg, priv);
	usrp_->send_info(cfg_.tg, cfg_.priv, cfg_.slot);
	usrp_->send_message(QString("TG %1%2").arg(tg).arg(priv ? " private" : ""));
	return QString();
}

bool Session::start()
{
	usrp_->set_tot_seconds(cfg_.tot_seconds);
	usrp_->set_intercom_tg(cfg_.intercom_tg);
	if (!usrp_->open(cfg_.usrp_port)) return false;
	voc_->load(cfg_.plugin_dir);
	for (const QString &l : voc_->log()) emit log("[보코더] " + l);
	if (!voc_->has_codec(VP_CODEC_AMBE2_FEC)) emit log("[보코더] DMR 코덱 없음 - 송신은 무음, 수신은 소리 없음");
	hb_->start(cfg_.master);
	rx_watch_->start(200);
	return true;
}

void Session::stop()
{
	if (tx_.active()) finish_tx("세션 종료");
	if (ic_tx_) finish_intercom_tx("세션 종료");
	if (hb_) hb_->stop();
	if (tx_timer_) tx_timer_->stop();
	if (rx_watch_) rx_watch_->stop();
	if (unlink_timer_) unlink_timer_->stop();
	unlinking_ = false;
	if (notice_timer_) notice_stop();
}

void Session::send_all(const std::vector<dmr::Dmrd> &v)
{
	for (const dmr::Dmrd &p : v) hb_->send(p);
}

void Session::on_ptt(bool keyed)
{
	if (!keyed) {
		ptt_latched_ = false;
		return;		// 실제 종료는 남은 음성을 다 보낸 뒤 on_tx_tick에서
	}
	if (notice_on_) notice_pause("클라이언트 송신");
	if (tx_.active() || ic_tx_) return;
	if (on_intercom() && !unlinking_) {		// 내부 채널: 마스터 접속·BM 짧은 송신 제한과 무관 (BM으로 안 나감)
		ic_tx_ = true;
		tx_started_.start();
		tx_rec_ = CallRecord();
		tx_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
		tx_rec_.tx = true; tx_rec_.src = cfg_.dmr_id; tx_rec_.dst = quint32(cfg_.tg); tx_rec_.priv = false;
		tx_rec_.slot = cfg_.slot; tx_rec_.kind = "intercom"; tx_rec_.src_call = cfg_.master.callsign;
		fill_names(tx_rec_);
		emit call_started(tx_rec_);
		emit log(QString("내부 채널 %1 송신 시작 (BM으로 안 나감)").arg(cfg_.tg));
		emit tx_state(true, cfg_.tg, false);
		tx_timer_->start(20);
		return;
	}
	if (unlinking_) {
		ptt_latched_ = true;
		emit log("송신 안 함: Unlink(4000) 송신 중 - 끝난 뒤 PTT를 다시 눌러 주세요");
		return;
	}
	if (!hb_->connected()) {
		emit log("송신 안 함: 마스터 미접속");
		usrp_->send_message("Not connected to master");
		return;
	}
	if (ptt_latched_) return;	// 막힌 PTT를 아직 누르고 있음
	if (short_tx_in_window() >= cfg_.short_tx_limit) {
		ptt_latched_ = true;
		const QList<qint64> &l = short_list(cfg_.dmr_id);
		const qint64 wait_ms = 60000 - (shared_clock().elapsed() - l.first());
		emit log(QString("송신 안 함: DMR ID %1 최근 60초 안에 2초 미만 송신 %2회 - 한 번 더 하면 BM 루프 방지로 1시간 차단 → %3초 뒤 가능")
				 .arg(cfg_.dmr_id).arg(l.size()).arg((wait_ms + 999) / 1000));
		usrp_->send_message("Too many short TX - wait");
		return;
	}
	dmr::FullLc lc;
	lc.flco = cfg_.priv ? dmr::kFlcoPrivate : dmr::kFlcoGroup;
	lc.src = cfg_.dmr_id;
	lc.dst = quint32(cfg_.tg);
	std::vector<dmr::Dmrd> out;
	tx_.set_talker_alias(make_talker_alias(cfg_.master.callsign, cfg_.device).toStdString());
	tx_.start(lc, cfg_.master.id, quint8(cfg_.slot), quint8(cfg_.cc), QRandomGenerator::global()->generate() | 1u, out);
	send_all(out);
	++tx_count_;
	tx_started_.start();
	if (!cfg_.priv && cfg_.tg != cfg_.unlink_tg) linked_tg_ = cfg_.tg;	// 그룹 TG로 송신 → BM에 동적 연결 생김
	tx_rec_ = CallRecord();
	tx_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
	tx_rec_.tx = true; tx_rec_.src = cfg_.dmr_id; tx_rec_.dst = quint32(cfg_.tg); tx_rec_.priv = cfg_.priv;
	tx_rec_.slot = cfg_.slot; tx_rec_.src_call = cfg_.master.callsign;
	fill_names(tx_rec_);
	emit call_started(tx_rec_);
	emit log(QString("송신 시작 → %1%2 (TS%3)").arg(cfg_.tg).arg(cfg_.priv ? " 개인" : " 그룹").arg(cfg_.slot));
	emit tx_state(true, cfg_.tg, cfg_.priv);
	tx_timer_->start(20);
}

void Session::on_tx_tick()
{
	if (ic_tx_) {		// 내부 채널: 휴대폰 소리를 그대로 같은 채널 사용자에게 (보코더·BM 없음)
		int16_t pcm[160];
		QVector<int16_t> out;
		int n = 0;
		while (n < 3 && usrp_->read_frame(pcm)) {
			out += QVector<int16_t>(pcm, pcm + 160);
			emit audio(QByteArray(reinterpret_cast<const char *>(pcm), int(sizeof(pcm))), true);
			++n;
		}
		if (!out.isEmpty()) emit intercom_audio(out, false);
		if (!usrp_->ptt() && usrp_->queued_frames() == 0) finish_intercom_tx("PTT 해제");
		return;
	}
	if (!tx_.active()) { tx_timer_->stop(); return; }
	std::vector<dmr::Dmrd> out;
	int16_t pcm[160];
	int n = 0;
	while (n < 3 && usrp_->read_frame(pcm)) {	// 한 틱에 최대 3프레임(밀린 것 따라잡기)
		uint8_t f[9];
		voc_->encode_dmr(pcm, f);
		emit audio(QByteArray(reinterpret_cast<const char *>(pcm), int(sizeof(pcm))), true);
		dmr::AmbeFrame a;
		std::copy(f, f + 9, a.begin());
		tx_.add_frame(a, out);
		++n;
	}
	send_all(out);
	if (!usrp_->ptt() && usrp_->queued_frames() == 0) finish_tx("PTT 해제");
	else if (!hb_->connected()) finish_tx("마스터 연결 끊김");
}

void Session::finish_tx(const QString &why)
{
	std::vector<dmr::Dmrd> out;
	tx_.end(out);
	send_all(out);
	tx_timer_->stop();
	last_tx_end_.start();
	const qint64 dur = tx_started_.elapsed();
	if (dur < 2000) short_list(cfg_.dmr_id).append(shared_clock().elapsed());
	emit log(QString("송신 종료 (%1, %2초)").arg(why).arg(dur / 1000.0, 0, 'f', 1));
	if (dur < 2000) emit log(QString("  2초 미만 송신 - 최근 60초 안 %1회 (한도 %2회)").arg(short_tx_in_window()).arg(cfg_.short_tx_limit));
	tx_rec_.duration_ms = dur;
	emit call_finished(tx_rec_);
	emit tx_state(false, cfg_.tg, cfg_.priv);
}

void Session::finish_intercom_tx(const QString &why)
{
	ic_tx_ = false;
	tx_timer_->stop();
	emit intercom_audio(QVector<int16_t>(), true);
	tx_rec_.duration_ms = tx_started_.elapsed();
	emit call_finished(tx_rec_);
	emit log(QString("내부 채널 송신 끝 (%1, %2초)").arg(why).arg(tx_started_.elapsed() / 1000.0, 0, 'f', 1));
	emit tx_state(false, cfg_.tg, false);
}

void Session::request_unlink(const QString &why)
{
	if (!hb_->connected()) { emit log("Unlink 안 함: 마스터 미접속"); return; }
	if (unlinking_) return;
	if (tx_.active()) { emit log("Unlink 안 함: 송신 중"); return; }
	dmr::FullLc lc;
	lc.flco = dmr::kFlcoGroup;
	lc.src = cfg_.dmr_id;
	lc.dst = quint32(cfg_.unlink_tg);
	dmr::VoiceTx u;
	unlink_q_.clear();
	u.start(lc, cfg_.master.id, quint8(cfg_.slot), quint8(cfg_.cc), QRandomGenerator::global()->generate() | 1u, unlink_q_);
	const int frames = qMax(3, cfg_.unlink_ms / 20);
	for (int i = 0; i < frames; ++i) u.add_frame(dmr::VoiceTx::silence(), unlink_q_);
	u.end(unlink_q_);
	unlink_pos_ = 0;
	unlinking_ = true;
	linked_tg_ = 0;
	++unlink_count_;
	emit log(QString("Unlink 송신 → %1 그룹 (%2, 무음 %3초)").arg(cfg_.unlink_tg).arg(why).arg(cfg_.unlink_ms / 1000.0, 0, 'f', 1));
	tx_rec_ = CallRecord();
	tx_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
	tx_rec_.tx = true; tx_rec_.src = cfg_.dmr_id; tx_rec_.dst = quint32(cfg_.unlink_tg); tx_rec_.priv = false;
	tx_rec_.slot = cfg_.slot; tx_rec_.kind = "unlink"; tx_rec_.src_call = cfg_.master.callsign;
	fill_names(tx_rec_);
	emit call_started(tx_rec_);
	emit tx_state(true, cfg_.unlink_tg, false);
	unlink_timer_->start(60);	// 실제 음성처럼 60ms마다 1패킷
	on_unlink_tick();
}

void Session::on_unlink_tick()
{
	if (unlink_pos_ < unlink_q_.size()) {
		hb_->send(unlink_q_[unlink_pos_++]);
		if (!hb_->connected()) unlink_pos_ = unlink_q_.size();
		if (unlink_pos_ < unlink_q_.size()) return;
	}
	unlink_timer_->stop();
	unlinking_ = false;
	last_tx_end_.start();
	tx_rec_.duration_ms = QDateTime::currentMSecsSinceEpoch() - tx_rec_.start_ms;
	emit call_finished(tx_rec_);
	emit log("Unlink 완료");
	emit tx_state(false, cfg_.tg, cfg_.priv);
}

// BM 루프 방지는 DMR ID 단위 → 같은 프로그램 안의 모든 세션이 DMR ID별 기록을 공유
QElapsedTimer &Session::shared_clock()
{
	static QElapsedTimer t;
	if (!t.isValid()) t.start();
	return t;
}

QList<qint64> &Session::short_list(quint32 dmr_id)
{
	static QHash<quint32, QList<qint64>> lists;
	return lists[dmr_id];
}

int Session::short_tx_in_window()
{
	QList<qint64> &l = short_list(cfg_.dmr_id);
	const qint64 now = shared_clock().elapsed();
	while (!l.isEmpty() && now - l.first() > 60000) l.removeFirst();
	return l.size();
}

void Session::on_dmrd(dmr::Dmrd p)
{
	if (tx_.active()) return;		// 내가 송신 중일 때 들어온 것은 재생하지 않음
	last_rx_pkt_.start();
	std::vector<dmr::VoiceRx::Event> ev;
	rx_.feed(p, ev);
	for (auto &e : ev) {
		if (notice_on_) notice_pause("BM 수신");		// BM 우선: 공지는 수신이 끝난 뒤 이어서
		if (e.kind == dmr::VoiceRx::Event::Start) {

			if (!rx_rec_active_ || rx_rec_.start_ms == 0) {
				rx_rec_ = CallRecord();
				rx_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
				rx_rec_active_ = true;
			}
			rx_rec_.tx = false; rx_rec_.src = e.lc.src; rx_rec_.dst = e.lc.dst; rx_rec_.priv = e.lc.is_private(); rx_rec_.slot = e.slot;
			rx_rec_.src_call.clear(); rx_rec_.src_name.clear();
			fill_names(rx_rec_);
			emit call_started(rx_rec_);
			// 휴대폰 앱 화면에 송신자 표시 (콜사인을 알면 콜사인)
			usrp_->send_call_info(e.lc.src, p.rptr, e.lc.dst, e.slot, cfg_.cc, e.lc.is_private(), rx_rec_.src_call);
			{	// DMRBridge 클라이언트용 상대방 정보 (이름·지역 포함)
				QJsonObject d{{"src", qint64(e.lc.src)}, {"call", rx_rec_.src_call}, {"name", rx_rec_.src_name},
							  {"dst", qint64(e.lc.dst)}, {"priv", e.lc.is_private()}, {"slot", e.slot}};
				if (ids_) if (const IdDatabase::Info *i = ids_->find(e.lc.src)) d["loc"] = i->loc;
				usrp_->send_call_detail(d);
			}
			emit log(QString("수신 시작 %1 → %2%3 (TS%4)").arg(ids_ ? ids_->display(e.lc.src) : QString::number(e.lc.src)).arg(e.lc.dst).arg(e.lc.is_private() ? " 개인" : " 그룹").arg(e.slot));
			emit rx_state(true, e.lc.src, e.lc.dst, e.lc.is_private());
		}
		else if (e.kind == dmr::VoiceRx::Event::Frames) {
			for (auto &f : e.frames) {
				int16_t pcm[160];
				voc_->decode_dmr(f.data(), pcm);
				usrp_->send_voice(pcm);
				emit audio(QByteArray(reinterpret_cast<const char *>(pcm), int(sizeof(pcm))), false);
			}
		}
		else if (e.kind == dmr::VoiceRx::Event::Alias) {
			const QString a = QString::fromStdString(e.alias).trimmed();
			emit log(QString("상대방 이름표(Talker Alias): %1").arg(a));
			emit rx_alias(a);
			QJsonObject d{{"src", qint64(rx_rec_.src)}, {"call", rx_rec_.src_call}, {"name", rx_rec_.src_name}, {"dst", qint64(rx_rec_.dst)},
						  {"priv", rx_rec_.priv}, {"slot", rx_rec_.slot}, {"alias", a}};
			if (ids_) if (const IdDatabase::Info *i = ids_->find(rx_rec_.src)) d["loc"] = i->loc;
			usrp_->send_call_detail(d);
		}
		else {
			usrp_->end_voice();
			finish_rx_record();
			emit log("수신 끝");
			emit rx_state(false, 0, 0, false);
		}
	}
}

void Session::on_rx_watch()
{
	if (reconfig_pending_) try_reconfig();
	if (rx_.active() && last_rx_pkt_.isValid() && last_rx_pkt_.elapsed() > 1000) {
		std::vector<dmr::VoiceRx::Event> ev;
		rx_.timeout(ev);
		finish_rx_record();
		usrp_->end_voice();
		emit rx_state(false, 0, 0, false);
	}
}

void Session::finish_rx_record()
{
	if (!rx_rec_active_) return;
	rx_rec_.duration_ms = QDateTime::currentMSecsSinceEpoch() - rx_rec_.start_ms;
	rx_rec_active_ = false;
	emit call_finished(rx_rec_);
	rx_rec_ = CallRecord();
}

bool Session::active_call(CallRecord &r) const
{
	if (tx_.active() || unlinking_ || ic_tx_) { r = tx_rec_; r.duration_ms = QDateTime::currentMSecsSinceEpoch() - r.start_ms; return true; }
	if (rx_rec_active_) { r = rx_rec_; r.duration_ms = QDateTime::currentMSecsSinceEpoch() - r.start_ms; return true; }
	if (notice_on_ && notice_rec_active_) { r = notice_rec_; r.duration_ms = QDateTime::currentMSecsSinceEpoch() - r.start_ms; return true; }
	return false;
}

QString Session::master_state_text() const
{
	static const char *names[] = {"정지", "주소 확인 중", "로그인 중", "인증 중", "설정 전송 중", "접속됨", "재시도 대기"};
	return QString::fromUtf8(names[int(hb_->state())]);
}

void Session::fill_names(CallRecord &r) const
{
	r.via = cfg_.name;	// 어느 사용자 브릿지인지 (다중 사용자)
	if (!ids_) return;
	if (const IdDatabase::Info *i = ids_->find(r.src)) {
		if (r.src_call.isEmpty()) r.src_call = i->call;
		r.src_name = i->name;
	}
}

QString Session::make_description(const QString &device)
{
	QString d = device.trimmed();
	if (d.startsWith("Galaxy ", Qt::CaseInsensitive)) d = d.mid(7).trimmed();
	QString clean;
	for (QChar ch : d) if (ch.unicode() >= 0x20 && ch.unicode() < 0x7F) clean += ch;	// RPTC는 ASCII 고정폭
	clean = clean.simplified();
	return (clean.isEmpty() ? QString("DMRBridge") : "DMRBridge " + clean).left(19);
}

void Session::on_client_info(const QJsonObject &o)
{
	ClientInfo c;
	c.call = o.value("call").toString().trimmed().toUpper().left(16);
	c.name = o.value("name").toString().trimmed().left(40);
	c.device = o.value("device").toString().trimmed().left(40);
	c.app = o.value("app").toString().trimmed().left(60);
	c.dmr_id = quint32(o.value("dmr_id").toInteger());
	const bool changed = c.call != client_.call || c.name != client_.name || c.device != client_.device || c.dmr_id != client_.dmr_id || c.app != client_.app;
	client_ = c;
	if (!changed) return;
	emit client_info_changed();
	emit log(QString("클라이언트 정보: %1%2%3 / 장비 %4%5").arg(c.call.isEmpty() ? "-" : c.call)
			 .arg(c.dmr_id ? QString(" (%1)").arg(c.dmr_id) : QString()).arg(c.name.isEmpty() ? QString() : " " + c.name)
			 .arg(c.device.isEmpty() ? "-" : c.device).arg(c.app.isEmpty() ? QString() : " · " + c.app));
	if (c.dmr_id && c.dmr_id != cfg_.dmr_id) {
		emit log(QString("주의: 클라이언트의 DMR ID %1이 서버 설정 %2와 다릅니다 - 송신은 서버 설정 ID로 나갑니다").arg(c.dmr_id).arg(cfg_.dmr_id));
		usrp_->send_message(QString("DMR ID differs: server uses %1").arg(cfg_.dmr_id));
	}
	if (c.device.isEmpty() || c.device == cfg_.device) return;
	cfg_.device = c.device;
	emit device_changed(c.device);
	if (make_location(c.device) != cfg_.master.location) { reconfig_pending_ = true; try_reconfig(); }
}

// 장비명 → BM 위치 칸 반영: 접속 중이면 통화가 없을 때 다시 접속(10분에 1번까지, 첫 번은 바로), 아니면 다음 로그인부터
void Session::try_reconfig()
{
	if (tx_.active() || unlinking_ || rx_.active()) return;		// 통화 중 → rx_watch에서 다시 시도
	const QString loc = make_location(cfg_.device);
	if (loc == cfg_.master.location) { reconfig_pending_ = false; relogin_wait_logged_ = false; return; }	// 기다리는 동안 원래 이름으로 돌아옴
	if (hb_->connected()) {
		const qint64 min_ms = qint64(qMax(0, cfg_.device_relogin_min_s)) * 1000;
		if (relogin_clock_.isValid() && relogin_clock_.elapsed() < min_ms) {
			reconfig_pending_ = true;
			if (!relogin_wait_logged_) {
				relogin_wait_logged_ = true;
				auto span = [](qint64 sec) { return sec >= 60 && sec % 60 == 0 ? QString("%1분").arg(sec / 60) : sec >= 60 ? QString("약 %1분").arg((sec + 59) / 60) : QString("%1초").arg(sec); };
				const qint64 left_s = (min_ms - relogin_clock_.elapsed() + 999) / 1000;
				emit log(QString("장비명 BM 반영은 %1 뒤 (재접속 %2에 1번 제한) - 이름표는 바로 적용").arg(span(left_s), span(min_ms / 1000)));
			}
			return;
		}
		reconfig_pending_ = false;
		relogin_wait_logged_ = false;
		cfg_.master.location = loc;
		cfg_.master.description = make_description(cfg_.device);
		relogin_clock_.start();
		emit log(QString("장비명 반영을 위해 BM 다시 접속: 위치 \"%1\"").arg(loc));
		hb_->stop();
		hb_->start(cfg_.master);
	}
	else {
		reconfig_pending_ = false;
		relogin_wait_logged_ = false;
		cfg_.master.location = loc;
		cfg_.master.description = make_description(cfg_.device);
		hb_->set_location(loc);
		hb_->set_description(cfg_.master.description);
		emit log(QString("장비명 저장: 다음 BM 로그인부터 위치 \"%1\"").arg(loc));
	}
}

QString Session::make_location(const QString &device)
{
	QString dev;
	for (QChar ch : device) if (ch.unicode() >= 0x20 && ch.unicode() < 0x7F) dev += ch;	// RPTC는 ASCII 고정폭
	dev = dev.simplified();
	if (dev.isEmpty()) return "DMRBridge";
	QString t = "DMRBridge " + dev;
	if (t.size() > 20 && dev.startsWith("Galaxy ", Qt::CaseInsensitive)) t = "DMRBridge " + dev.mid(7).trimmed();
	return t.left(20);
}

QString Session::make_talker_alias(const QString &callsign, const QString &device)
{
	auto ascii = [](const QString &x) {
		QString o;
		for (QChar ch : x) if (ch.unicode() >= 0x20 && ch.unicode() < 0x7F) o += ch;
		return o.simplified();
	};
	const QString call = ascii(callsign.toUpper());
	QString dev = ascii(device);
	QString t = (call.isEmpty() ? QString() : call + " ") + "DMRBridge" + (dev.isEmpty() ? QString() : " " + dev);
	if (t.size() > 31 && dev.startsWith("Galaxy ", Qt::CaseInsensitive)) {
		dev = dev.mid(7);
		t = (call.isEmpty() ? QString() : call + " ") + "DMRBridge " + dev;
	}
	return t.left(31);
}

// ---------------- 브릿지 공지 (BM과 무관, 이 사용자의 휴대폰·클라이언트에만) ----------------

void Session::notice_add(const QString &title, const QString &source, const QVector<int16_t> &pcm8k, bool ai)
{
	if (pcm8k.isEmpty()) return;
	if (!usrp_->has_client()) { emit log("공지 건너뜀: 휴대폰이 아직 연결되지 않음"); return; }
	NoticeClip c;
	c.queued.start();
	c.title = title.simplified().left(40);
	c.source = source;
	c.pcm = pcm8k;
	c.ai = ai;
	notice_q_.append(c);
	emit log(QString("공지 대기: %1 (%2초)").arg(c.title).arg(pcm8k.size() / 8000.0, 0, 'f', 1));
	if (!notice_timer_->isActive()) notice_timer_->start(20);
}

void Session::notice_live(const QVector<int16_t> &pcm8k, bool end)
{
	NoticeClip *c = nullptr;
	for (NoticeClip &x : notice_q_) if (x.open && x.source == "live") c = &x;
	if (!c) {
		if (end && pcm8k.isEmpty()) return;
		if (!usrp_->has_client()) return;		// 휴대폰이 없는 사용자는 실시간 방송을 받지 않음
		NoticeClip n;
		n.title = QStringLiteral("실시간 마이크");
		n.source = "live";
		n.open = true;
		n.last_add.start();
		n.queued.start();
		notice_q_.append(n);
		c = &notice_q_.last();
		emit log("공지: 실시간 마이크 방송 시작");
	}
	c->pcm += pcm8k;
	c->last_add.start();
	if (end) { c->open = false; emit log("공지: 실시간 마이크 방송 끝"); }
	if (!notice_timer_->isActive()) notice_timer_->start(20);
}

void Session::intercom_feed(const QString &key, quint32 src_id, const QString &call, const QString &name, const QVector<int16_t> &pcm8k, bool end)
{
	if (!on_intercom()) return;			// 이 사용자는 내부 채널에 없음
	NoticeClip *c = nullptr;
	for (NoticeClip &x : notice_q_) if (x.open && x.source == "intercom" && x.key == key) c = &x;
	if (!c) {
		if (end || pcm8k.isEmpty() || !usrp_->has_client()) return;
		NoticeClip n;
		n.source = "intercom";
		n.queued.start();
		n.key = key; n.call = call; n.title = name; n.src_id = src_id;
		n.open = true;
		n.last_add.start();
		notice_q_.append(n);
		c = &notice_q_.last();
	}
	c->pcm += pcm8k;
	c->last_add.start();
	if (end) c->open = false;
	if (!notice_timer_->isActive()) notice_timer_->start(20);
}

bool Session::notice_active() const { return notice_on_ && !notice_q_.isEmpty() && notice_q_.first().source != "intercom"; }
int Session::notice_waiting() const { int n = 0; for (const NoticeClip &c : notice_q_) n += c.source != "intercom"; return n; }

QJsonObject Session::notice_status() const
{
	if (notice_q_.isEmpty() || notice_q_.first().source == "intercom") return QJsonObject{{"state", "idle"}};
	const NoticeClip &c = notice_q_.first();
	QJsonObject o{{"title", c.title}, {"total_s", c.pcm.size() / 8000.0}, {"live", c.open}, {"waiting", notice_waiting()}};
	if (notice_on_) { o["state"] = "sending"; o["progress"] = c.pcm.isEmpty() ? 0.0 : double(notice_pos_) / c.pcm.size(); o["pos_s"] = notice_pos_ / 8000.0; }
	else { o["state"] = "waiting"; o["reason"] = notice_wait_reason_; }
	return o;
}

void Session::notice_stop()
{
	if (notice_on_) notice_finish("취소");
	else if (notice_rec_active_) notice_finish("취소");
	if (!notice_q_.isEmpty()) emit log(QString("공지 취소: 대기 중 %1건").arg(notice_q_.size()));
	notice_q_.clear();
	notice_pos_ = 0;
	notice_resume_ = false;
	notice_timer_->stop();
}

void Session::notice_begin()
{
	const NoticeClip &c = notice_q_.first();
	if (c.source == "intercom") {		// 내부 채널: 말하는 사람 콜사인으로 표시 (그룹 호출처럼)
		const quint32 tg = quint32(cfg_.intercom_tg);
		usrp_->send_call_info(c.src_id, 0, tg, cfg_.slot, cfg_.cc, false, c.call);
		QJsonObject d{{"intercom", true}, {"src", qint64(c.src_id)}, {"dst", qint64(tg)}, {"priv", false}, {"slot", cfg_.slot},
					  {"call", c.call}, {"name", c.title}};
		if (notice_resume_) d["resume"] = true;
		usrp_->send_call_detail(d);
		if (!notice_rec_active_) {
			notice_rec_ = CallRecord();
			notice_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
			notice_rec_.tx = false; notice_rec_.src = c.src_id; notice_rec_.dst = tg; notice_rec_.priv = false;
			notice_rec_.slot = cfg_.slot; notice_rec_.kind = "intercom";
			notice_rec_.src_call = c.call; notice_rec_.src_name = c.title; notice_rec_.via = cfg_.name;
			notice_rec_active_ = true;
			emit call_started(notice_rec_);
		}
		emit log(QString("내부 채널 수신: %1").arg(c.call));
		notice_on_ = true;
		notice_clock_.start();
		notice_sent_ = 0;
		return;
	}
	const QString label = notice_label(c.ai);
	// 휴대폰 화면 이름표 + DMRBridge 클라이언트용 상세 정보
	usrp_->send_call_info(cfg_.dmr_id, 0, cfg_.dmr_id, cfg_.slot, cfg_.cc, true, label);
	QJsonObject d{{"notice", true}, {"src", qint64(cfg_.dmr_id)}, {"dst", qint64(cfg_.dmr_id)}, {"priv", true}, {"slot", cfg_.slot},
				  {"call", label}, {"name", c.title}, {"source", c.source}};
	if (notice_resume_) d["resume"] = true;
	usrp_->send_call_detail(d);
	if (!notice_rec_active_) {
		notice_rec_ = CallRecord();
		notice_rec_.start_ms = QDateTime::currentMSecsSinceEpoch();
		notice_rec_.tx = true; notice_rec_.src = cfg_.dmr_id; notice_rec_.dst = cfg_.dmr_id; notice_rec_.priv = true;
		notice_rec_.slot = cfg_.slot; notice_rec_.kind = "notice";
		notice_rec_.src_call = label; notice_rec_.src_name = c.title; notice_rec_.via = cfg_.name;
		notice_rec_active_ = true;
		emit call_started(notice_rec_);
	}
	emit log(QString("공지 %1: %2").arg(notice_resume_ ? "이어서 보냄" : "보냄").arg(c.title));
	notice_on_ = true;
	notice_clock_.start();
	notice_sent_ = 0;
}

void Session::notice_pause(const QString &why)
{
	if (!notice_on_) return;
	usrp_->end_voice();
	notice_on_ = false;
	notice_resume_ = true;
	notice_idle_.start();
	if (!notice_q_.isEmpty() && notice_q_.first().source == "intercom") emit log(QString("내부 채널 수신 멈춤 (%1)").arg(why));
	else emit log(QString("공지 잠시 멈춤 (%1) - 끝나면 이어서 보냄").arg(why));
}

void Session::notice_finish(const QString &why)
{
	if (notice_on_) usrp_->end_voice();
	notice_on_ = false;
	notice_resume_ = false;
	notice_idle_.start();
	if (notice_rec_active_) {
		notice_rec_.duration_ms = QDateTime::currentMSecsSinceEpoch() - notice_rec_.start_ms;
		notice_rec_active_ = false;
		emit call_finished(notice_rec_);
	}
	const bool ic = !notice_q_.isEmpty() && notice_q_.first().source == "intercom";
	emit log(QString(ic ? "내부 채널 수신 끝 (%1)" : "공지 끝 (%1)").arg(why));
	if (!notice_q_.isEmpty()) notice_q_.removeFirst();
	notice_pos_ = 0;
}

void Session::on_notice_tick()
{
	if (notice_q_.isEmpty()) { notice_timer_->stop(); return; }
	NoticeClip &c = notice_q_.first();
	// 실시간 마이크가 5초 넘게 끊기면 (브라우저를 닫는 등) 방송 끝으로 처리
	if (c.open && c.last_add.isValid() && c.last_add.elapsed() > 5000) { c.open = false; emit log("공지: 실시간 마이크 신호 끊김 → 방송 끝"); }
	const bool busy = rx_.active() || tx_.active() || unlinking_ || usrp_->ptt();
	if (!notice_on_ && !notice_resume_ && c.queued.isValid()) {	// 못 보내고 너무 오래 기다림 → 버림 (실시간·내부 채널 10초, 문장 10분)
		const bool live = c.source == "live" || c.source == "intercom";
		if (c.queued.elapsed() > (live ? 10000 : 600000)) {
			emit log(QString("%1 버림: %2 동안 보내지 못함 (통화 중이거나 휴대폰 연결 끊김)").arg(c.source == "intercom" ? "내부 채널 소리" : "공지").arg(live ? "10초" : "10분"));
			notice_q_.removeFirst();
			notice_pos_ = 0;
			if (notice_q_.isEmpty()) notice_timer_->stop();
			return;
		}
	}
	if (busy || !usrp_->has_client()) {
		notice_wait_reason_ = rx_.active() ? "BM 수신이 끝나기를 기다리는 중" : (tx_.active() || usrp_->ptt()) ? "휴대폰 송신이 끝나기를 기다리는 중"
							  : unlinking_ ? "Unlink가 끝나기를 기다리는 중" : "휴대폰 연결을 기다리는 중";
		if (notice_on_) notice_pause(busy ? "통화 중" : "클라이언트 없음");
		if (busy) notice_idle_.start();
		if (c.open) notice_pos_ = c.pcm.size();		// 실시간 마이크: 기다리는 동안의 말은 버림 (늦게 나가지 않게)
		return;
	}
	if (!notice_on_) {
		if (notice_idle_.isValid() && notice_idle_.elapsed() < 1500) { notice_wait_reason_ = "통화가 막 끝나 잠깐 쉬는 중"; return; }	// 통화가 끝난 직후 잠깐 쉼
		if (c.open && c.pcm.size() - notice_pos_ < 160 * 10) { notice_wait_reason_ = "마이크 소리를 모으는 중"; return; }	// 실시간: 0.2초 모이면 시작
		if (!c.open && notice_pos_ >= c.pcm.size()) { notice_finish("끝"); return; }
		if (notice_resume_ && !c.open) notice_pos_ = qMax(0, notice_pos_ - 8000);	// 이어서 보낼 때 1초 앞부터
		notice_begin();
	}
	qint64 due = notice_clock_.elapsed() / 20 + 1 - notice_sent_;
	if (due > 5) due = 5;
	while (due-- > 0) {
		const int left = c.pcm.size() - notice_pos_;
		if (left < 160 && c.open) {			// 실시간: 아직 안 온 소리 → 시계를 다시 맞추고 기다림
			notice_clock_.start(); notice_sent_ = 0;
			break;
		}
		if (left <= 0) { notice_finish("끝"); return; }
		int16_t pcm[160] = {0};
		std::copy(c.pcm.constBegin() + notice_pos_, c.pcm.constBegin() + notice_pos_ + qMin(160, left), pcm);
		notice_pos_ += qMin(160, left);
		usrp_->send_voice(pcm);
		emit audio(QByteArray(reinterpret_cast<const char *>(pcm), int(sizeof(pcm))), false);
		++notice_sent_;
	}
	if (c.open && notice_pos_ > 8000 * 30) { c.pcm.remove(0, notice_pos_); notice_pos_ = 0; }	// 긴 방송 메모리 정리
}
