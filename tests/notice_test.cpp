// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// 브릿지 공지 시험 — PC 내부만 (BM·마스터 없음, 휴대폰에만 가는 음성이므로 마스터 접속이 필요 없음)
//   1) NoticeService 도구: 알림음·WAV 읽기·크기 맞춤·예약 시각 판정·설정 검사
//   2) Session 공지: 자체 클라이언트(type 빅엔디안) → SET_INFO "브릿지 공지"(UTF-8) + INFO:CALL notice + 음성 + keyup 0
//      DVSwitch Mobile(type 리틀엔디안) → 같은 "브릿지 공지"(한글 표시됨), INFO:CALL 없음
//      바이트 순서 모름(음성만 받음) → 빅엔디안 다음 리틀엔디안 순서로 둘 다
//      클라이언트 PTT가 끼어들면 멈췄다가 놓은 뒤 이어서 (resume), 실시간 마이크 조각 이어 붙이기, 취소
//   3) (이 PC에 음성 합성 목소리가 있으면) 실제 문장 읽기 WAV 만들기
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QThread>
#include <QUdpSocket>
#include <QtEndian>
#include "notice_service.h"
#include "session/session.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

static bool wait_until(const std::function<bool()> &cond, int ms)
{
	QElapsedTimer t; t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		if (cond()) return true;
		QThread::msleep(2);
	}
	return cond();
}
static void pump(int ms) { wait_until([] { return false; }, ms); }

struct Client {
	QUdpSocket s;
	quint16 srv = 0;
	bool le = false;
	quint32 seq = 0;
	int voice = 0, unkey = 0, peak = 0;
	QList<QByteArray> set_info, set_info_other;	// other = 반대 바이트 순서로 온 것
	QString order;		// 이름표가 온 순서: S = 내 순서, O = 반대 순서
	QList<QJsonObject> calls;
	QList<QByteArray> infos, msgs;		// INFO:{json}, INFO:MSG:글
	bool reg_ok = false, non_ascii = false;	// non_ascii: TEXT에 ASCII 아닌 바이트가 있었음
	void send(quint32 type, bool keyup, const QByteArray &payload, int size = 352)
	{
		QByteArray d(qMax(32, size), '\0');
		uchar *h = reinterpret_cast<uchar *>(d.data());
		std::memcpy(h, "USRP", 4);
		qToBigEndian<quint32>(seq++, h + 4);
		qToBigEndian<quint32>(keyup ? 1 : 0, h + 12);
		if (le) qToLittleEndian<quint32>(type, h + 20); else qToBigEndian<quint32>(type, h + 20);
		std::memcpy(h + 32, payload.constData(), qMin(int(payload.size()), d.size() - 32));
		s.writeDatagram(d, QHostAddress::LocalHost, srv);
	}
	void hello() { send(3, false, "PING", 32 + 5); }		// type 칸으로 바이트 순서를 서버가 알게 함
	void drain()
	{
		while (s.hasPendingDatagrams()) {
			const QByteArray d = s.receiveDatagram().data();
			if (d.size() < 32 || std::memcmp(d.constData(), "USRP", 4)) continue;
			const uchar *h = reinterpret_cast<const uchar *>(d.constData());
			const quint32 keyup = qFromBigEndian<quint32>(h + 12);
			const quint32 type = le ? qFromLittleEndian<quint32>(h + 20) : qFromBigEndian<quint32>(h + 20);
			if (type == 0x02000000u && d.size() > 33 && uchar(d[32]) == 8) { set_info_other.append(d.mid(32)); order += "O"; continue; }
			if (type == 0) {
				if (keyup && d.size() >= 352) {
					++voice;
					for (int i = 0; i < 160; ++i) peak = qMax(peak, qAbs(int(qFromLittleEndian<qint16>(h + 32 + i * 2))));
				} else if (!keyup) ++unkey;
			} else if (type == 2) {
				const QByteArray pl = d.mid(32);
				const bool tlv8 = !pl.isEmpty() && uchar(pl[0]) == 8;	// 이름표: 앞 14바이트는 숫자 칸, 콜사인 부분만 검사
				for (char ch : tlv8 ? pl.mid(14) : pl) if (uchar(ch) >= 0x80) non_ascii = true;
				if (pl.startsWith("REG:OK")) reg_ok = true;
				else if (pl.startsWith("INFO:MSG:")) msgs.append(pl.mid(9, pl.indexOf('\0') - 9));
				else if (pl.startsWith("INFO:{")) infos.append(pl.mid(5, pl.indexOf('\0') - 5));
				else if (pl.startsWith("INFO:CALL:")) calls.append(QJsonDocument::fromJson(pl.mid(10, pl.indexOf('\0') - 10)).object());
				else if (!pl.isEmpty() && uchar(pl[0]) == 8) { set_info.append(pl); order += "S"; }
			}
		}
	}
	void reset() { voice = unkey = peak = 0; set_info.clear(); set_info_other.clear(); calls.clear(); order.clear(); infos.clear(); msgs.clear(); non_ascii = false; }
	static QByteArray label(const QByteArray &tlv) { const QByteArray c = tlv.mid(2 + 12); return c.left(c.indexOf('\0')); }
};

static QVector<int16_t> tone(double sec, double f = 700, int amp = 6000)
{
	QVector<int16_t> v(int(sec * 8000));
	for (int i = 0; i < v.size(); ++i) v[i] = int16_t(amp * std::sin(2 * M_PI * f * i / 8000));
	return v;
}

static QByteArray make_wav(const QVector<int16_t> &pcm)
{
	QByteArray w("RIFF____WAVEfmt ");
	auto put32 = [&](quint32 x) { char b[4]; qToLittleEndian(x, b); w.append(b, 4); };
	auto put16 = [&](quint16 x) { char b[2]; qToLittleEndian(x, b); w.append(b, 2); };
	put32(16); put16(1); put16(1); put32(8000); put32(16000); put16(2); put16(16);
	w.append("LIST"); put32(4); w.append("INFO");		// 다른 조각도 건너뛰는지
	w.append("data"); put32(quint32(pcm.size() * 2));
	for (int16_t s : pcm) put16(quint16(s));
	return w;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	QCoreApplication app(argc, argv);

	std::printf("[1] 공지 소리 도구\n");
	{
		const QVector<int16_t> ch = NoticeService::chime();
		int pk = 0; for (int16_t s : ch) pk = qMax(pk, qAbs(int(s)));
		CHECK(ch.size() >= 8000 && ch.size() <= 14000 && pk > 4000 && pk < 32000, "딩동 알림음 1.0~1.75초, 크기 적당");
		QVector<int16_t> got; QString err;
		CHECK(NoticeService::parse_wav(make_wav(tone(0.5)), got, &err) && got.size() == 4000, "WAV 읽기 (8kHz 16bit 모노, 다른 조각 건너뜀)");
		QByteArray bad = make_wav(tone(0.1)); qToLittleEndian<quint32>(16000, bad.data() + 24);
		CHECK(!NoticeService::parse_wav(bad, got, &err), "8kHz가 아닌 WAV는 거부");
		QVector<int16_t> quiet = tone(1.0, 500, 600);
		NoticeService::normalize(quiet);
		double e = 0; for (int16_t s : quiet) e += double(s) * s;
		const double rms = std::sqrt(e / quiet.size());
		CHECK(rms > 2500 && rms < 4500, "작은 말소리 크기 키우기 (RMS 약 3500)");
		const QVector<int16_t> b = NoticeService::build(tone(1.0), true, 2);
		CHECK(b.size() == 800 + ch.size() + 8000 + 8000 + 8000 + 1600, "알림음 + 2번 반복(사이 1초) 이어 붙이기");
		QJsonArray sch{QJsonObject{{"id", "a"}, {"on", true}, {"time", "09:00"}, {"days", QJsonArray{1, 3}}, {"text", "x"}},
					   QJsonObject{{"id", "b"}, {"on", false}, {"time", "09:00"}, {"days", QJsonArray{1}}, {"text", "y"}}};
		const QDateTime mon9(QDate(2026, 11, 16), QTime(9, 0, 30));		// 2026-11-16 = 월요일
		CHECK(NoticeService::due(sch, mon9).size() == 1 && NoticeService::due(sch, mon9)[0]["id"] == "a", "예약: 월 09:00 → 켜진 것만");
		CHECK(NoticeService::due(sch, mon9.addDays(1)).isEmpty(), "예약: 화요일은 안 울림");
		CHECK(NoticeService::due(sch, mon9.addSecs(60)).isEmpty(), "예약: 09:01은 안 울림");
		QString why;
		CHECK(NoticeService::sanitize(QJsonObject{{"schedule", QJsonArray{QJsonObject{{"time", "25:00"}, {"days", QJsonArray{1}}, {"text", "x"}}}}}, &why).isEmpty() && !why.isEmpty(), "설정 검사: 잘못된 시각 거부");
		CHECK(NoticeService::sanitize(QJsonObject{{"schedule", QJsonArray{QJsonObject{{"time", "08:30"}, {"days", QJsonArray{}}, {"text", "x"}}}}}, &why).isEmpty(), "설정 검사: 요일 없음 거부");
		const QJsonObject ok = NoticeService::sanitize(QJsonObject{{"rate", 99}, {"schedule", QJsonArray{QJsonObject{{"time", "08:30"}, {"days", QJsonArray{7, 1, 9}}, {"text", " 안내 "}, {"repeat", 7}}}}}, &why);
		const QJsonObject e0 = ok["schedule"].toArray().at(0).toObject();
		CHECK(ok["rate"].toInt() == 10 && e0["days"].toArray().size() == 2 && e0["repeat"].toInt() == 3 && e0["text"] == "안내" && !e0["id"].toString().isEmpty(),
			  "설정 검사: 범위 맞춤·요일 정리·id 자동");
	}

	std::printf("[1-1] 무전 교신 말투 (호출부호·TG·발음 사전)\n");
	{
		const QString d = NoticeService::default_dict();
		auto rt = [&](const char *t, const char *style) { return NoticeService::radio_text(QString::fromUtf8(t), style, d); };
		CHECK(rt("HL0EXM입니다", "phonetic") == QString::fromUtf8("호텔 리마 제로 에코 엑스레이 마이크입니다"), "HL0EXM → 호텔 리마 제로 에코 엑스레이 마이크");
		CHECK(rt("HL0EXM", "letters") == QString::fromUtf8("에이치 엘 제로 이 엑스 엠"), "영어 글자: HL0EXM → 에이치 엘 제로 이 엑스 엠");
		CHECK(rt("ds1abc 국", "korean") == QString::fromUtf8("디 에스 일 에이 비 씨 국"), "한국어 숫자·소문자: ds1abc → 디 에스 일 에이 비 씨");
		CHECK(rt("TG 450에서", "phonetic") == QString::fromUtf8("티지 포 파이브 제로에서") && rt("TG450", "korean") == QString::fromUtf8("티지 사 오 공"), "TG 450 → 티지 포 파이브 제로 / 사 오 공");
		CHECK(rt("CQ CQ, 73!", "phonetic") == QString::fromUtf8("씨큐 씨큐, 세븐티 쓰리!"), "사전: CQ·73");
		CHECK(rt("2026년 A17 DMR", "phonetic") == QString::fromUtf8("2026년 A17 디엠알"), "호출부호 아닌 것(연도·A17)은 그대로");
		CHECK(rt("BM1ABC", "phonetic") == QString::fromUtf8("브라보 마이크 원 알파 브라보 찰리"), "사전이 호출부호 일부(BM)를 바꾸지 않음");
		CHECK(NoticeService::radio_text("HAM", "phonetic", "HAM=햄 라디오") == QString::fromUtf8("햄 라디오"), "사용자 발음 사전 적용");
	}
	std::printf("[1-2] BM 위치 칸 (장비명)\n");
	CHECK(Session::make_location("Galaxy A17") == "DVBridge Galaxy A17", "Galaxy A17 → \"DVBridge Galaxy A17\" (20자)");
	CHECK(Session::make_location("Galaxy S24 Ultra") == "DVBridge S24 Ultra", "20자 넘으면 Galaxy 생략");
	CHECK(Session::make_location("PC") == "DVBridge PC" && Session::make_location("") == "DVBridge", "짧은 이름·빈 이름");
	CHECK(Session::make_location("승훈의 A17") == "DVBridge A17" && Session::make_location("A Very Long Device Name X").size() == 20, "한글 빼고, 20자로 자름");
	std::printf("[2] 세션 공지 (BM과 무관)\n");
	SessionConfig cfg;
	cfg.name = "HL0EXM";
	cfg.master.host = "127.0.0.1"; cfg.master.port = 62998; cfg.master.id = 450000099; cfg.master.callsign = "HL0EXM"; cfg.master.password = "x";
	cfg.dmr_id = 4500000;
	cfg.usrp_port = 32961;
	Session s(cfg);
	CHECK(s.start(), "세션 시작 (마스터 없음)");
	Client c;
	c.s.bind(QHostAddress::LocalHost, 0);
	c.srv = cfg.usrp_port;
	c.hello();
	pump(200);

	// 자체 클라이언트 (type 빅엔디안)
	c.reset();
	s.notice_add("정기 교신 안내", "tts", tone(1.0));
	CHECK(wait_until([&] { c.drain(); return c.unkey > 0; }, 4000), "공지 끝까지 보냄 (keyup 0)");
	CHECK(c.set_info.size() == 1 && Client::label(c.set_info[0]) == QByteArray("브릿지 공지"), "자체 클라이언트 이름표 UTF-8 \"브릿지 공지\"");
	CHECK(c.calls.size() == 1 && c.calls[0]["notice"].toBool() && c.calls[0]["name"] == "정기 교신 안내" && c.calls[0]["source"] == "tts" && c.calls[0]["src"].toInteger() == 4500000,
		  "INFO:CALL notice=true·제목·출처·src");
	CHECK(c.voice >= 49 && c.voice <= 51 && c.peak > 5000, "음성 1초 = 50프레임");
	CHECK(!s.notice_active() && s.notice_waiting() == 0, "공지 끝 → 대기 없음");

	// 클라이언트 PTT가 끼어들면 멈췄다가 이어서
	c.reset();
	s.notice_add("긴 공지", "schedule", tone(3.0));
	wait_until([&] { c.drain(); return c.voice >= 50; }, 3000);
	for (int i = 0; i < 25; ++i) { c.send(0, true, QByteArray(320, '\0')); pump(20); }	// 0.5초 PTT
	c.drain();
	CHECK(c.unkey >= 1 && !s.notice_active(), "클라이언트 PTT → 공지 멈춤");
	c.send(0, false, QByteArray());
	CHECK(wait_until([&] { c.drain(); return c.calls.size() >= 2; }, 4000) && c.calls.last()["resume"].toBool(), "PTT 놓은 뒤 이어서 보냄 (resume)");
	CHECK(wait_until([&] { c.drain(); return s.notice_waiting() == 0; }, 6000), "이어서 끝까지");
	CHECK(c.voice >= 150, "멈춘 곳 1초 앞부터 다시 (총 150프레임 이상)");

	// 실시간 마이크 조각 + 끝
	c.reset();
	for (int k = 0; k < 5; ++k) { s.notice_live(tone(0.2), false); pump(200); }
	s.notice_live(QVector<int16_t>(), true);
	CHECK(wait_until([&] { c.drain(); return c.unkey > 0 && s.notice_waiting() == 0; }, 4000), "실시간 마이크 1초 → 보내고 끝");
	CHECK(c.calls.size() == 1 && c.calls[0]["source"] == "live" && c.voice >= 45, "실시간: source live, 음성 대부분 전달");

	// 취소
	c.reset();
	s.notice_add("취소될 공지", "tts", tone(5.0));
	s.notice_add("대기 공지", "tts", tone(1.0));
	wait_until([&] { c.drain(); return c.voice >= 10; }, 2000);
	s.notice_stop();
	pump(300); c.drain();
	const int v = c.voice;
	pump(500); c.drain();
	CHECK(!s.notice_active() && s.notice_waiting() == 0 && c.unkey >= 1 && c.voice == v, "공지 모두 멈춤 → 바로 끊고 대기도 지움");

	// DVSwitch Mobile (type 리틀엔디안)
	Client m;
	m.le = true;
	m.s.bind(QHostAddress::LocalHost, 0);
	m.srv = cfg.usrp_port;
	m.hello();
	pump(300);
	m.reset();
	s.notice_add("DVSM", "tts", tone(0.5));
	CHECK(wait_until([&] { m.drain(); return m.unkey > 0; }, 4000), "DVSwitch Mobile에도 공지 전달");
	CHECK(m.set_info.size() == 1 && Client::label(m.set_info[0]) == QByteArray("브릿지 공지") && m.set_info_other.isEmpty(), "DVSwitch Mobile 이름표 \"브릿지 공지\" (UTF-8, 한 번만)");
	CHECK(m.calls.isEmpty(), "DVSwitch Mobile에는 INFO:CALL 안 보냄");

	// 음성만 보낸 클라이언트 (바이트 순서 모름): 이름표를 두 순서로 하나씩, INFO:CALL은 안 보냄
	Client u;
	u.s.bind(QHostAddress::LocalHost, 0);
	u.srv = cfg.usrp_port;
	u.send(0, false, QByteArray(), 32);		// keyup 0 음성 머리만
	pump(300);
	u.reset();
	s.notice_add("모름", "tts", tone(0.5));
	CHECK(wait_until([&] { u.drain(); return u.unkey > 0; }, 4000), "순서 모르는 클라이언트에도 공지 전달");
	CHECK(u.set_info.size() == 1 && Client::label(u.set_info[0]) == QByteArray("브릿지 공지") &&
		  u.set_info_other.size() == 1 && Client::label(u.set_info_other[0]) == QByteArray("브릿지 공지") && u.order == "SO",
		  "순서 모름 → 빅엔디안 다음 리틀엔디안 \"브릿지 공지\" 둘 다 (DVSwitch Mobile용이 마지막)");
	CHECK(u.calls.isEmpty(), "순서 모르는 클라이언트에는 INFO:CALL 안 보냄");

	// pyUC 계열 USRP Client (type 리틀엔디안, 접속 뒤 자기 정보 + INFO: 요청, 글자는 ASCII로 읽음)
	std::printf("[2-1] pyUC 계열 USRP Client\n");
	Client py;
	py.le = true;
	py.s.bind(QHostAddress::LocalHost, 0);
	py.srv = cfg.usrp_port;
	py.send(2, false, QByteArray("REG:DVSWITCH", 13), 32 + 13);
	CHECK(wait_until([&] { py.drain(); return py.reg_ok; }, 2000), "REG:DVSWITCH → REG:OK (리틀엔디안)");
	{
		QByteArray si;		// [8][len][DMR ID 3][중계기 4][TG 3][TS][CC][콜사인\0]
		const QByteArray call = "HL0PYU";
		si.append(char(8)); si.append(char(3 + 4 + 3 + 1 + 1 + call.size() + 1));
		const quint32 id = 4500002;
		si.append(char(id >> 16)); si.append(char(id >> 8)); si.append(char(id));
		si.append(QByteArray(4 + 3 + 1 + 1, '\0')); si.append(call); si.append('\0');
		py.send(2, false, si, 32 + si.size());
	}
	py.send(2, false, QByteArray("INFO:", 6), 32 + 6);
	CHECK(wait_until([&] { py.drain(); return !py.infos.isEmpty(); }, 2000), "INFO: 요청 → INFO json 보냄 (DVSwitch Mobile에는 안 보냄)");
	{
		const QJsonObject o = py.infos.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(py.infos.last()).object();
		CHECK(o["tlv"].toObject()["ambe_mode"].toString() == "DMR" && o.contains("last_tune"), "INFO json에 tlv.ambe_mode·last_tune");
	}
	CHECK(s.client_info().call == "HL0PYU" && s.client_info().dmr_id == 4500002, "자기 정보 SET_INFO → 클라이언트 콜사인·DMR ID 표시");
	py.reset();
	s.notice_add("공지 제목", "tts", tone(0.5));
	CHECK(wait_until([&] { py.drain(); return py.unkey > 0; }, 4000), "pyUC에도 공지 전달");
	CHECK(!py.set_info.isEmpty() && (uchar(py.set_info[0][13]) & 0x80) == 0, "pyUC에는 공지를 그룹으로 (개인호출이면 답장 채널로 바뀜)");
	CHECK(py.set_info.size() == 1 && Client::label(py.set_info[0]) == QByteArray("BRIDGE NOTICE") && !py.non_ascii, "pyUC 이름표는 ASCII \"BRIDGE NOTICE\" (한글 없음)");
	py.reset();
	py.send(1, false, QByteArray("*YSF", 5), 32 + 5);
	CHECK(wait_until([&] { py.drain(); return !py.msgs.isEmpty(); }, 2000) && py.msgs.last().startsWith("DMR only") && !py.non_ascii, "DMR 말고 다른 모드 요청 → INFO:MSG \"DMR only\"");
	int tg_changes = 0;
	QObject::connect(&s, &Session::target_changed, [&](int, bool) { ++tg_changes; });
	py.send(1, false, QByteArray("91", 3), 32 + 3);		// pyUC는 DTMF와 원격 명령 tgs=를 함께 보냄
	{
		QByteArray tl; const QByteArray cmd = "tgs=91"; tl.append(char(5)); tl.append(char(cmd.size())); tl.append(cmd);
		py.send(4, false, tl, 32 + tl.size());
	}
	pump(400); py.drain();
	CHECK(tg_changes == 1, "같은 채널 변경(DTMF 91 + tgs=91)은 한 번만 처리");
	CHECK(!py.non_ascii && !py.msgs.isEmpty() && py.msgs.last().startsWith("TG 91"), "채널 변경 안내도 ASCII");
	s.stop();

	std::printf("[2-2] 내부 채널 (BM으로 안 나감)\n");
	{
		auto mk = [&](const char *call, quint32 hs, quint16 port) {
			SessionConfig k = cfg;
			k.name = call; k.master.callsign = call; k.master.id = hs; k.usrp_port = port; k.intercom_tg = 99;
			return k;
		};
		Session a(mk("HL0AAA", 450000011, 32971)), b(mk("HL0BBB", 450000012, 32972)), x(mk("HL0XXX", 450000013, 32973));
		a.start(); b.start(); x.start();
		auto wire = [&](Session &from, QList<Session *> to) {
			QObject::connect(&from, &Session::intercom_audio, [&from, to](QVector<int16_t> pcm, bool end) {
				for (Session *o : to) o->intercom_feed(QString::number(from.config().master.id), from.config().dmr_id, from.config().master.callsign, from.config().name, pcm, end);
			});
		};
		wire(a, {&b, &x});
		x.notice_add("휴대폰 없음", "tts", tone(0.5));
		CHECK(x.notice_waiting() == 0, "휴대폰이 없는 사용자에게는 공지를 쌓지 않음");
		CHECK(a.set_target(99, false, "시험").isEmpty() && b.set_target(99, true, "시험").isEmpty() && a.on_intercom() && b.on_intercom() && !b.config().priv,
			  "99 → 내부 채널 (개인호출 표시는 무시)");
		Client ca, cb, cx;
		for (Client *k : {&ca, &cb, &cx}) k->s.bind(QHostAddress::LocalHost, 0);
		ca.srv = 32971; cb.srv = 32972; cx.srv = 32973;
		for (Client *k : {&ca, &cb, &cx}) k->hello();
		pump(300);
		for (Client *k : {&ca, &cb, &cx}) { k->drain(); k->reset(); }
		CallRecord last; bool got = false;
		QObject::connect(&a, &Session::call_finished, [&](CallRecord r) { last = r; got = true; });
		for (int i = 0; i < 50; ++i) { QByteArray f(320, '\0'); for (int k = 0; k < 160; ++k) qToLittleEndian<qint16>(qint16(5000 * std::sin(2 * M_PI * 600 * (i * 160 + k) / 8000.0)), f.data() + k * 2); ca.send(0, true, f); pump(20); }
		ca.send(0, false, QByteArray());
		CHECK(wait_until([&] { cb.drain(); return cb.unkey > 0; }, 5000), "A가 내부 채널로 말함 → B 휴대폰에 전달 (keyup 0까지)");
		CHECK(cb.set_info.size() >= 1 && Client::label(cb.set_info[0]) == QByteArray("HL0AAA"), "B 화면 이름표 = 말한 사람 콜사인");
		CHECK(cb.calls.size() >= 1 && cb.calls[0]["intercom"].toBool() && cb.calls[0]["src"].toInteger() == 4500000 && cb.calls[0]["dst"].toInt() == 99, "INFO:CALL intercom=true, src·dst(99)");
		CHECK(cb.voice >= 40 && cb.peak > 4000, "B에 소리 대부분 전달 (BM 보코더 거치지 않음)");
		pump(300); cx.drain();
		CHECK(cx.voice == 0 && cx.set_info.isEmpty(), "내부 채널에 없는 X는 못 들음");
		CHECK(got && last.kind == "intercom" && last.tx && last.dst == 99, "A 이력: 내부 채널 송신");
		CHECK(a.tx_count() == 0 && a.short_tx_in_window() == 0, "BM 송신 0번 (짧은 송신 집계에도 안 들어감)");
		CHECK(b.set_target(450, false, "시험").isEmpty() && !b.on_intercom(), "B가 450#로 BM 채널로 돌아감");
		cb.reset();
		for (int i = 0; i < 30; ++i) { ca.send(0, true, QByteArray(320, '\x10')); pump(20); }
		ca.send(0, false, QByteArray());
		pump(2500); cb.drain();
		CHECK(cb.voice == 0, "BM 채널로 돌아간 B는 내부 채널 소리를 안 받음");
		a.stop(); b.stop(); x.stop();
	}
	std::printf("[2-3] 내부 에코·시험 모드 (BM으로 안 나감)\n");
	{
		auto mk = [&](const char *call, quint32 hs, quint16 port, bool test) {
			SessionConfig k = cfg;
			k.name = call; k.master.callsign = call; k.master.id = hs; k.usrp_port = port; k.echo_tg = 9999; k.test_mode = test;
			return k;
		};
		auto speak = [&](Client &c, int frames) {
			for (int i = 0; i < frames; ++i) {
				QByteArray f(320, '\0');
				for (int k = 0; k < 160; ++k) qToLittleEndian<qint16>(qint16(5000 * std::sin(2 * M_PI * 600 * (i * 160 + k) / 8000.0)), f.data() + k * 2);
				c.send(0, true, f); pump(20);
			}
			c.send(0, false, QByteArray());
		};
		// 내부 에코: 9999#로 들어가 말하면 되돌려 들려줌
		Session e(mk("HL0ECO", 450000021, 32981, false));
		e.start();
		Client ce; ce.s.bind(QHostAddress::LocalHost, 0); ce.srv = 32981; ce.hello(); pump(300); ce.drain(); ce.reset();
		CHECK(e.set_target(9999, false, "시험").isEmpty() && e.on_echo() && !e.config().priv, "9999 → 내부 에코");
		QList<CallRecord> recs;
		QObject::connect(&e, &Session::call_finished, [&](CallRecord r) { recs.append(r); });
		speak(ce, 50);	// 1초
		CHECK(wait_until([&] { ce.drain(); return ce.unkey > 0 && ce.voice >= 45; }, 6000), "1초 말함 → 내 목소리를 되돌려 들려줌");
		CHECK(ce.set_info.size() >= 1 && Client::label(ce.set_info[0]) == QByteArray("에코 시험"), "화면 이름표 \"에코 시험\"");
		CHECK(ce.calls.size() >= 1 && ce.calls[0]["echo"].toBool() && ce.calls[0]["source"] == "echo", "INFO:CALL echo=true");
		CHECK(ce.peak > 4000, "되돌아온 소리 크기 그대로");
		CHECK(e.tx_count() == 0 && e.short_tx_in_window() == 0, "BM 송신 0번 (짧은 송신 집계에도 안 들어감)");
		CHECK(recs.size() >= 2 && recs[0].kind == "echo" && recs[0].tx && recs.last().kind == "echo" && !recs.last().tx, "이력: 에코 녹음(송신) + 에코 들려줌(수신)");
		ce.reset();
		speak(ce, 10);	// 0.2초 - 너무 짧음
		pump(2500); ce.drain();
		CHECK(ce.voice == 0, "0.5초보다 짧으면 되돌려 주지 않음");
		e.stop();
		// 시험 모드: BM에 접속하지 않고, 어떤 TG로 말해도 에코
		Session t(mk("HL0TST", 450000022, 32982, true));
		CHECK(t.start(), "시험 모드 세션 시작");
		pump(1500);
		CHECK(!t.master()->connected() && t.master_state_text().startsWith(QString::fromUtf8("시험 모드")), "시험 모드: BM에 접속하지 않음");
		Client ct; ct.s.bind(QHostAddress::LocalHost, 0); ct.srv = 32982; ct.hello(); pump(300); ct.drain(); ct.reset();
		CHECK(t.set_target(450, false, "시험").isEmpty() && t.config().tg == 450, "시험 모드에서도 채널 바꾸기 (BM으로는 안 나감)");
		speak(ct, 50);
		CHECK(wait_until([&] { ct.drain(); return ct.unkey > 0 && ct.voice >= 45; }, 6000), "TG 450에서 말해도 에코로 되돌아옴");
		CHECK(t.tx_count() == 0, "시험 모드: BM 송신 0번");
		t.stop();
	}
	{
		QVector<int16_t> t = tone(0.3), back; QString err;
		CHECK(NoticeService::parse_wav(NoticeService::make_wav(t), back, &err) && back == t, "미리 듣기 WAV 만들기·읽기 왕복 동일");
	}

	std::printf("[3] 문장 읽기 (Windows 음성 합성)\n");
	{
		NoticeService ns;
		wait_until([&] { return ns.tts_available() || ns.voices().size() || false; }, 15000);
		if (!ns.tts_available()) std::printf("  [SKIP] 이 PC에 음성 합성 목소리 없음\n");
		else {
			QVector<int16_t> got;
			QObject::connect(&ns, &NoticeService::ready, [&](QVector<int16_t> p, QString, QString, QStringList, bool) { got = p; });
			NoticeService::Job j; j.text = "브릿지 공지 시험입니다."; j.chime = true;
			ns.speak(j);
			CHECK(wait_until([&] { return !got.isEmpty(); }, 30000), "한국어 문장 → 음성 만들기");
			CHECK(got.size() > NoticeService::chime().size() + 8000, "알림음 + 말소리 1초 이상");
		}
	}

	std::printf("[4] 음성 팩 (자연스러운 목소리, 실행 파일 옆 voice 폴더가 있을 때만)\n");
	{
		NoticeService ns;
		wait_until([&] { return ns.tts_available(); }, 15000);
		if (!ns.voicepack()) std::printf("  [SKIP] voice 폴더 없음\n");
		else {
			bool any_st = false;
			for (const QJsonValue &v : ns.voices()) any_st |= v.toObject()["name"].toString().startsWith("st:");
			CHECK(any_st, "목소리 목록에 자연스러운 목소리(st:F1 등)");
			QVector<int16_t> got; bool ai = true;
			QObject::connect(&ns, &NoticeService::ready, [&](QVector<int16_t> p, QString, QString, QStringList, bool a) { got = p; ai = a; });
			NoticeService::Job j; j.text = "브릿지 공지 시험입니다."; j.chime = false;
			ns.set_config(QJsonObject{{"voice", ""}, {"ai_agree", false}});
			ns.speak(j);
			CHECK(wait_until([&] { return !got.isEmpty(); }, 60000) && !ai, "동의 안 함 → Windows 목소리 (AI 표시 없음)");
			got.clear();
			ns.set_config(QJsonObject{{"voice", ""}, {"ai_agree", true}});
			ns.speak(j);
			CHECK(wait_until([&] { return !got.isEmpty(); }, 90000) && ai, "동의함 → 자연스러운 목소리 (AI 표시)");
			CHECK(got.size() > 8000, "문장 1초 이상");
		}
	}

	std::printf(g_fail ? "notice_test: %d FAIL\n" : "notice_test: ALL PASS\n", g_fail);
	return g_fail ? 1 : 0;
}
