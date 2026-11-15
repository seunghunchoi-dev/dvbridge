// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "notice_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
// 음성 합성 (PowerShell 5.1, Windows 기본). 입력·출력·목소리·빠르기는 환경 변수로 받음 (따옴표 문제 없음)
const char *kTtsScript =
	"$ErrorActionPreference='Stop'\n"
	"Add-Type -AssemblyName System.Speech\n"
	"$t=[IO.File]::ReadAllText($env:DVB_TTS_IN,[Text.Encoding]::UTF8)\n"
	"$s=New-Object System.Speech.Synthesis.SpeechSynthesizer\n"
	"$vs=@($s.GetInstalledVoices() | Where-Object { $_.Enabled })\n"
	"$v=$null\n"
	"if ($env:DVB_TTS_VOICE) { $v=$vs | Where-Object { $_.VoiceInfo.Name -eq $env:DVB_TTS_VOICE } | Select-Object -First 1 }\n"
	"if (-not $v) { $v=$vs | Where-Object { $_.VoiceInfo.Culture.Name -eq 'ko-KR' } | Select-Object -First 1 }\n"
	"if ($v) { $s.SelectVoice($v.VoiceInfo.Name) }\n"
	"$s.Rate=[int]$env:DVB_TTS_RATE\n"
	"$f=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(8000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)\n"
	"$s.SetOutputToWaveFile($env:DVB_TTS_OUT,$f)\n"
	"$s.Speak($t)\n"
	"$s.Dispose()\n";
const char *kVoicesScript =
	"Add-Type -AssemblyName System.Speech\n"
	"$s=New-Object System.Speech.Synthesis.SpeechSynthesizer\n"
	"$s.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { 'VOICE|' + $_.VoiceInfo.Name + '|' + $_.VoiceInfo.Culture.Name }\n";

QStringList ps_args(const char *script)
{
	const QString s = QString::fromUtf8(script);
	QByteArray u16(reinterpret_cast<const char *>(s.utf16()), int(s.size() * 2));
	return {"-NoProfile", "-NonInteractive", "-EncodedCommand", QString::fromLatin1(u16.toBase64())};
}
} // namespace

NoticeService::NoticeService(QObject *parent) : QObject(parent)
{
	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, &NoticeService::tick);
	timer_->start(1000);
	load_voices();
}

NoticeService::~NoticeService()
{
	if (proc_) { proc_->kill(); proc_->waitForFinished(2000); }
}

QString NoticeService::powershell()
{
	return QDir(qEnvironmentVariable("SystemRoot", "C:\\Windows")).filePath("System32/WindowsPowerShell/v1.0/powershell.exe");
}

QString NoticeService::voicepack_dir()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath("voice");
}

void NoticeService::load_voices()
{
	// 음성 팩: 실행 파일·ONNX Runtime·모델이 모두 있어야 씀
	const QDir vd(voicepack_dir());
	if (QFile::exists(vd.filePath("dvbridge_tts.exe")) && QFile::exists(vd.filePath("onnxruntime.dll")) &&
		QFile::exists(vd.filePath("supertonic3/onnx/vector_estimator.onnx"))) {
		pack_exe_ = vd.filePath("dvbridge_tts.exe");
		pack_model_ = QDir::toNativeSeparators(vd.filePath("supertonic3"));
	}
	auto *p = new QProcess(this);
	connect(p, &QProcess::finished, this, [this, p](int, QProcess::ExitStatus) {
		voices_ = QJsonArray();
		if (!pack_exe_.isEmpty()) {		// 자연스러운 목소리 (음성 팩) - 목록 맨 앞
			const QStringList st = QDir(pack_model_ + "/voice_styles").entryList({"*.json"}, QDir::Files, QDir::Name);
			for (const QString &f : st) {
				const QString id = f.left(f.size() - 5);
				const QString who = id.startsWith('F') ? QString("여성 %1").arg(id.mid(1)) : id.startsWith('M') ? QString("남성 %1").arg(id.mid(1)) : id;
				voices_.append(QJsonObject{{"name", "st:" + id}, {"culture", "ko-KR"}, {"label", "자연스러운 목소리(AI) · " + who}, {"engine", "supertonic"}});
			}
		}
		for (const QString &line : QString::fromUtf8(p->readAllStandardOutput()).split('\n')) {
			const QStringList f = line.trimmed().split('|');
			if (f.size() == 3 && f[0] == "VOICE") voices_.append(QJsonObject{{"name", f[1]}, {"culture", f[2]}, {"label", "Windows 목소리 · " + f[1]}, {"engine", "windows"}});
		}
		voices_ok_ = true;
		emit log(voices_.isEmpty() ? QString("공지: 음성 합성 목소리가 없습니다 - 문장 읽기 공지는 쓸 수 없고 마이크 방송만 됩니다")
								   : QString("공지: 음성 합성 목소리 %1개%2").arg(voices_.size()).arg(pack_exe_.isEmpty() ? QString() : QString(" (음성 팩: 자연스러운 목소리 사용)")));
		p->deleteLater();
	});
	p->start(powershell(), ps_args(kVoicesScript));
}

QString NoticeService::default_dict()
{
	return QStringLiteral("CQ=씨큐\nQSL=큐에스엘\nQRZ=큐알지\nQTH=큐티에이치\nQSO=큐에스오\nQRM=큐알엠\nQSY=큐에스와이\n"
						  "73=세븐티 쓰리\n88=에이티 에잇\nDMR=디엠알\nBM=브랜드마이스터\nRF=알에프\nOM=오엠\nYL=와이엘\nHAM=햄");
}

QString NoticeService::radio_text(const QString &text, const QString &style, const QString &dict)
{
	static const char *phon[26] = {"알파", "브라보", "찰리", "델타", "에코", "폭스트롯", "골프", "호텔", "인디아", "줄리엣", "킬로", "리마", "마이크",
								   "노벰버", "오스카", "파파", "퀘벡", "로미오", "시에라", "탱고", "유니폼", "빅터", "위스키", "엑스레이", "양키", "줄루"};
	static const char *let[26] = {"에이", "비", "씨", "디", "이", "에프", "지", "에이치", "아이", "제이", "케이", "엘", "엠",
								  "엔", "오", "피", "큐", "알", "에스", "티", "유", "브이", "더블유", "엑스", "와이", "제트"};
	static const char *en_d[10] = {"제로", "원", "투", "쓰리", "포", "파이브", "식스", "세븐", "에잇", "나인"};
	static const char *ko_d[10] = {"공", "일", "이", "삼", "사", "오", "육", "칠", "팔", "구"};
	const bool korean = style == "korean", letters = style == "letters";
	auto digit = [&](QChar c) { return QString::fromUtf8(korean ? ko_d[c.digitValue()] : en_d[c.digitValue()]); };
	auto spell = [&](const QString &w) {
		QStringList parts;
		for (QChar c : w) {
			if (c.isDigit()) parts << digit(c);
			else if (c.isLetter() && c.toUpper().unicode() >= 'A' && c.toUpper().unicode() <= 'Z') {
				const int i = c.toUpper().unicode() - 'A';
				parts << QString::fromUtf8((letters || korean) ? let[i] : phon[i]);
			}
		}
		return parts.join(' ');
	};
	QString t = text;
	// 1) 발음 사전 (긴 단어부터, 영문·숫자 단어는 단어 경계에서만)
	QList<QPair<QString, QString>> pairs;
	for (const QString &line : dict.split('\n')) {
		const int eq = line.indexOf('=');
		if (eq <= 0) continue;
		const QString from = line.left(eq).trimmed(), to = line.mid(eq + 1).trimmed();
		if (!from.isEmpty()) pairs.append({from, to});
	}
	std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
	// 2) 호출부호: 앞부분(1~3자, 글자 하나 이상) + 숫자 + 끝 글자 1~4개 (예 HL0EXM, DS1ABC, W1AW). 뒤의 ·90 같은 접미사는 숫자로
	static const QRegularExpression call(R"((?<![A-Za-z0-9])((?=[A-Za-z0-9]{0,2}[A-Za-z])[A-Za-z0-9]{1,3}[0-9][A-Za-z]{1,4})(?:[·\-/]([0-9]{1,2}))?(?![A-Za-z0-9]))");
	// 3) TG 번호: "TG 450", "TG450", "티지 450" → 한 자리씩
	static const QRegularExpression tg(R"((?<![A-Za-z0-9])(?:TG|tg|티지|토크그룹)\s*([0-9]{1,7})(?![0-9]))");
	QString out;
	int pos = 0;
	// 순서: TG → 호출부호 → 사전 (사전이 호출부호 일부를 바꾸지 않게 자리표로 보호)
	auto it = tg.globalMatch(t);
	while (it.hasNext()) {
		const auto m = it.next();
		out += t.mid(pos, m.capturedStart() - pos);
		QStringList ds;
		for (QChar c : m.captured(1)) ds << digit(c);
		out += QString::fromUtf8("티지 ") + ds.join(' ');
		pos = m.capturedEnd();
	}
	out += t.mid(pos);
	t = out; out.clear(); pos = 0;
	QStringList keep;		// 바꾼 호출부호 (사전 적용에서 빼기)
	auto ic = call.globalMatch(t);
	while (ic.hasNext()) {
		const auto m = ic.next();
		out += t.mid(pos, m.capturedStart() - pos);
		QString sp = spell(m.captured(1));
		if (!m.captured(2).isEmpty()) sp += ", " + spell(m.captured(2));
		out += QString("\x01%1\x02").arg(keep.size());
		keep << sp;
		pos = m.capturedEnd();
	}
	out += t.mid(pos);
	t = out;
	for (const auto &p : pairs) {
		bool word = true;
		for (QChar c : p.first) if (!(c.isLetterOrNumber() && c.unicode() < 128)) word = false;
		if (word) t.replace(QRegularExpression("(?<![A-Za-z0-9])" + QRegularExpression::escape(p.first) + "(?![A-Za-z0-9])", QRegularExpression::CaseInsensitiveOption), p.second);
		else t.replace(p.first, p.second);
	}
	for (int i = 0; i < keep.size(); ++i) t.replace(QString("\x01%1\x02").arg(i), keep[i]);
	return t;
}

QJsonObject NoticeService::status() const
{
	QJsonObject o{{"queue", jobs_.size()}};
	if (proc_) {
		o["making"] = true;
		o["text"] = cur_.text.left(40);
		o["elapsed_ms"] = job_clock_.elapsed();
		o["estimate_ms"] = job_estimate_ms_;
		o["preview"] = cur_.source == "preview";
		o["ai"] = cur_ai_;
	}
	return o;
}

QJsonObject NoticeService::sanitize(const QJsonObject &in, QString *err)
{
	QJsonObject o;
	o["voice"] = in.value("voice").toString().trimmed().left(100);
	o["rate"] = qBound(-10, in.value("rate").toInt(0), 10);
	o["chime"] = in.value("chime").toBool(true);
	o["ai_agree"] = in.value("ai_agree").toBool(false);	// 음성 팩 모델(Open RAIL-M) 사용 제한에 동의
	{	const QString cs = in.value("callstyle").toString();
		o["callstyle"] = (cs == "letters" || cs == "korean") ? cs : QString("phonetic");
		o["dict"] = in.contains("dict") ? in.value("dict").toString().left(3000) : default_dict(); }
	QJsonArray out;
	static const QRegularExpression hm("^([01]\\d|2[0-3]):[0-5]\\d$");
	const QJsonArray sch = in.value("schedule").toArray();
	if (sch.size() > 50) { if (err) *err = "예약은 50개까지 만들 수 있습니다"; return {}; }
	for (const QJsonValue &v : sch) {
		const QJsonObject s = v.toObject();
		QJsonObject e;
		QString id = s.value("id").toString().trimmed();
		if (id.isEmpty() || id.size() > 40) id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
		e["id"] = id;
		e["on"] = s.value("on").toBool(true);
		const QString t = s.value("time").toString().trimmed();
		if (!hm.match(t).hasMatch()) { if (err) *err = QString("예약 시각이 잘못되었습니다: %1 (예 09:00)").arg(t); return {}; }
		e["time"] = t;
		QJsonArray days;
		for (int d = 1; d <= 7; ++d) for (const QJsonValue &x : s.value("days").toArray()) if (x.toInt() == d) { days.append(d); break; }
		if (days.isEmpty()) { if (err) *err = QString("%1 예약에 요일을 하나 이상 골라 주세요").arg(t); return {}; }
		e["days"] = days;
		const QString text = s.value("text").toString().trimmed();
		if (text.isEmpty()) { if (err) *err = QString("%1 예약의 안내 문장이 비어 있습니다").arg(t); return {}; }
		if (text.size() > 500) { if (err) *err = "안내 문장은 500자까지입니다"; return {}; }
		e["text"] = text;
		QJsonArray tg;
		for (const QJsonValue &x : s.value("targets").toArray()) if (!x.toString().trimmed().isEmpty()) tg.append(x.toString().trimmed());
		e["targets"] = tg;
		e["chime"] = s.value("chime").toBool(true);
		e["repeat"] = qBound(1, s.value("repeat").toInt(1), 3);
		out.append(e);
	}
	o["schedule"] = out;
	return o;
}

void NoticeService::speak(const Job &j)
{
	if (j.text.trimmed().isEmpty()) return;
	if (jobs_.size() >= 20) { emit log("공지: 만들 문장이 너무 많이 밀려 있어 이번 것은 건너뜀"); return; }
	jobs_.append(j);
	if (!proc_) next_job();
}

void NoticeService::next_job()
{
	if (proc_ || jobs_.isEmpty()) return;
	cur_ = jobs_.takeFirst();
	const QString base = QDir::temp().filePath(QString("dvbridge_tts_%1").arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0')));
	cur_in_ = base + ".txt"; cur_out_ = base + ".wav";
	QFile f(cur_in_);
	if (!f.open(QIODevice::WriteOnly)) { emit log("공지: 임시 파일을 만들 수 없습니다"); QTimer::singleShot(0, this, &NoticeService::next_job); return; }
	f.write(radio_text(cur_.text, cfg_.value("callstyle").toString("phonetic"), cfg_.contains("dict") ? cfg_.value("dict").toString() : default_dict()).toUtf8());
	f.close();
	proc_ = new QProcess(this);
	// 목소리: "st:F1" = 음성 팩, 비우면 음성 팩이 있으면 그것(F1), 없으면 Windows 한국어 목소리
	//   음성 팩은 웹에서 모델 사용 제한에 동의(ai_agree)해야 씀
	QString voice = cfg_.value("voice").toString();
	if (voice.isEmpty() && voicepack_usable()) voice = "st:F1";
	if (voice.startsWith("st:") && !voicepack_usable()) voice.clear();	// 음성 팩이 없거나 동의 안 함 → Windows 목소리
	cur_ai_ = voice.startsWith("st:");
	job_clock_.start();
	job_estimate_ms_ = qint64((cur_ai_ ? 3500 : 1200) + cur_.text.size() * (cur_ai_ ? ms_per_char_st_ : ms_per_char_win_));
	const int rate = qBound(-10, cfg_.value("rate").toInt(0), 10);
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("DVB_TTS_IN", cur_in_);
	env.insert("DVB_TTS_OUT", cur_out_);
	env.insert("DVB_TTS_VOICE", voice);
	env.insert("DVB_TTS_RATE", QString::number(qBound(-10, rate - 2, 10)));	// Windows 목소리 "보통"은 무전으로 듣기에 빨라서 두 단계 느리게
	proc_->setProcessEnvironment(env);
	QTimer::singleShot(90000, proc_, [p = proc_] { if (p->state() != QProcess::NotRunning) p->kill(); });	// 멈추면 90초 뒤 끝냄
	connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
		QFile w(cur_out_);
		QVector<int16_t> pcm;
		QString err;
		const bool ok = st == QProcess::NormalExit && code == 0 && w.open(QIODevice::ReadOnly) && parse_wav(w.readAll(), pcm, &err) && !pcm.isEmpty();
		w.close();
		if (!ok) {
			const QString e = QString::fromLocal8Bit(proc_->readAllStandardError()).simplified().left(200);
			emit log(QString("공지: 문장 읽기 실패 %1").arg(err.isEmpty() ? e : err));
		}
		QFile::remove(cur_in_); QFile::remove(cur_out_);
		proc_->deleteLater(); proc_ = nullptr;
		if (ok) {
			normalize(pcm);
			{	// 걸린 시간으로 예상 시간 맞추기 (글자당, 천천히 따라감)
				const double per = double(qMax<qint64>(0, job_clock_.elapsed() - (cur_ai_ ? 3500 : 1200))) / qMax(1, int(cur_.text.size()));
				double &m = cur_ai_ ? ms_per_char_st_ : ms_per_char_win_;
				m = qBound(5.0, 0.7 * m + 0.3 * per, 400.0);
			}
			emit ready(build(pcm, cur_.chime, cur_.repeat), cur_.text, cur_.source, cur_.targets, cur_ai_);
		}
		next_job();
	});
	if (voice.startsWith("st:")) {
		// 빠르기: 웹 -5~5 → 0.9(무전에서 또박또박 들리는 보통 빠르기) 기준 ±6%씩. 0.7 아래는 도우미가 받지 않음
		const double speed = qBound(0.7, 0.9 * (1.0 + 0.06 * rate), 2.0);
		proc_->start(pack_exe_, {"--model", pack_model_, "--voice", voice.mid(3), "--speed", QString::number(speed, 'f', 2),
								 "--in", QDir::toNativeSeparators(cur_in_), "--out", QDir::toNativeSeparators(cur_out_)});
	}
	else proc_->start(powershell(), ps_args(kTtsScript));
}

bool NoticeService::parse_wav(const QByteArray &w, QVector<int16_t> &pcm, QString *err)
{
	auto u32 = [&](int at) { return quint32(uchar(w[at])) | quint32(uchar(w[at + 1])) << 8 | quint32(uchar(w[at + 2])) << 16 | quint32(uchar(w[at + 3])) << 24; };
	auto u16 = [&](int at) { return quint16(uchar(w[at]) | uchar(w[at + 1]) << 8); };
	if (w.size() < 12 || !w.startsWith("RIFF") || w.mid(8, 4) != "WAVE") { if (err) *err = "WAV 형식이 아님"; return false; }
	int pos = 12;
	bool fmt_ok = false;
	while (pos + 8 <= w.size()) {
		const QByteArray id = w.mid(pos, 4);
		const qint64 len = u32(pos + 4);
		const int body = pos + 8;
		if (id == "fmt " && len >= 16 && body + 16 <= w.size()) {
			if (u16(body) != 1 || u16(body + 2) != 1 || u32(body + 4) != 8000 || u16(body + 14) != 16) { if (err) *err = "8kHz 16bit 모노가 아님"; return false; }
			fmt_ok = true;
		}
		else if (id == "data") {
			if (!fmt_ok) { if (err) *err = "fmt 없음"; return false; }
			const int n = int(qMin<qint64>(len, w.size() - body) / 2);
			pcm.resize(n);
			for (int i = 0; i < n; ++i) pcm[i] = int16_t(u16(body + i * 2));
			return true;
		}
		pos = body + int(len) + int(len & 1);
	}
	if (err) *err = "data 없음";
	return false;
}

QByteArray NoticeService::make_wav(const QVector<int16_t> &pcm)
{
	QByteArray w;
	auto u32 = [&](quint32 x) { for (int i = 0; i < 4; ++i) w.append(char((x >> (8 * i)) & 0xFF)); };
	auto u16 = [&](quint16 x) { w.append(char(x & 0xFF)); w.append(char(x >> 8)); };
	const quint32 n = quint32(pcm.size() * 2);
	w.append("RIFF"); u32(36 + n); w.append("WAVE");
	w.append("fmt "); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
	w.append("data"); u32(n);
	for (int16_t s : pcm) u16(quint16(s));
	return w;
}

void NoticeService::normalize(QVector<int16_t> &pcm)
{
	// 말소리 구간(20ms 단위로 조용하지 않은 곳)의 RMS를 약 3500으로 (BM 음성 복원 크기와 비슷), 피크는 부드럽게 제한
	double sum = 0; qint64 cnt = 0;
	for (int i = 0; i + 160 <= pcm.size(); i += 160) {
		double e = 0;
		for (int k = 0; k < 160; ++k) e += double(pcm[i + k]) * pcm[i + k];
		if (e / 160 > 200.0 * 200.0) { sum += e; cnt += 160; }
	}
	if (!cnt) return;
	const double g = qBound(0.5, 3500.0 / std::sqrt(sum / cnt), 6.0);
	for (int16_t &s : pcm) {
		double x = s * g / 32767.0;
		x = std::tanh(x * 1.2) / std::tanh(1.2);		// 부드러운 제한
		s = int16_t(qBound(-32767.0, x * 30000.0, 32767.0));
	}
}

QVector<int16_t> NoticeService::chime()
{
	// 딩(미5) - 동(도5): 종소리처럼 빠르게 울리고 천천히 사라짐
	const int sr = 8000;
	QVector<double> b(int(sr * 1.25), 0.0);
	auto note = [&](double f, double at, double len) {
		const int s0 = int(at * sr), n = int(len * sr);
		for (int i = 0; i < n && s0 + i < b.size(); ++i) {
			const double t = double(i) / sr;
			const double env = qMin(1.0, t / 0.005) * std::exp(-t / 0.32);
			b[s0 + i] += env * (std::sin(2 * M_PI * f * t) + 0.25 * std::sin(4 * M_PI * f * t) + 0.08 * std::sin(6 * M_PI * f * t));
		}
	};
	note(659.25, 0.0, 0.9);
	note(523.25, 0.45, 0.8);
	QVector<int16_t> out(b.size() + sr / 4, 0);		// 뒤에 0.25초 쉼
	for (int i = 0; i < b.size(); ++i) out[i] = int16_t(qBound(-32767.0, b[i] * 8000.0, 32767.0));
	return out;
}

QVector<int16_t> NoticeService::build(const QVector<int16_t> &speech, bool with_chime, int repeat)
{
	QVector<int16_t> out(800, 0);		// 앞 0.1초 (휴대폰이 소리를 켜는 시간)
	if (with_chime) out += chime();
	for (int r = 0; r < qBound(1, repeat, 3); ++r) {
		if (r) out += QVector<int16_t>(8000, 0);	// 반복 사이 1초
		out += speech;
	}
	out += QVector<int16_t>(1600, 0);	// 뒤 0.2초
	return out;
}

QList<QJsonObject> NoticeService::due(const QJsonArray &schedule, const QDateTime &now)
{
	QList<QJsonObject> r;
	const QString hm = now.toString("HH:mm");
	const int dow = now.date().dayOfWeek();
	for (const QJsonValue &v : schedule) {
		const QJsonObject s = v.toObject();
		if (!s.value("on").toBool(true) || s.value("time").toString() != hm) continue;
		bool day = false;
		for (const QJsonValue &d : s.value("days").toArray()) if (d.toInt() == dow) day = true;
		if (day) r.append(s);
	}
	return r;
}

void NoticeService::tick()
{
	const QDateTime now = QDateTime::currentDateTime();
	const QString stamp = now.toString("yyyyMMddHHmm");
	for (const QJsonObject &s : due(cfg_.value("schedule").toArray(), now)) {
		const QString key = s.value("id").toString() + "|" + stamp;
		if (fired_.contains(key)) continue;
		fired_.insert(key);
		Job j;
		j.text = s.value("text").toString();
		j.source = "schedule";
		for (const QJsonValue &t : s.value("targets").toArray()) j.targets << t.toString();
		j.chime = s.value("chime").toBool(true);
		j.repeat = s.value("repeat").toInt(1);
		emit log(QString("공지 예약 실행 %1: %2").arg(s.value("time").toString(), j.text.left(40)));
		recent_.prepend(QJsonObject{{"at", now.toString("MM-dd HH:mm")}, {"text", j.text.left(60)}});
		while (recent_.size() > 10) recent_.removeLast();
		speak(j);
	}
	if (fired_.size() > 200)		// 오래된 기록 정리 (이번 분 것은 남겨 같은 분에 두 번 울리지 않게)
		for (auto it = fired_.begin(); it != fired_.end(); ) it = it->endsWith("|" + stamp) ? std::next(it) : fired_.erase(it);
}
