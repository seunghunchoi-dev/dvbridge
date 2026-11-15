// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// NoticeService — 브릿지 공지 음성 만들기 + 예약(스케줄러)
//   문장 읽기: Windows 기본 음성 합성(System.Speech, 한국어 목소리 "Microsoft Heami" 등)을 PowerShell로 불러
//     8kHz 16bit 모노 WAV를 만들고, 알림음(딩동)·반복을 붙여 ready 신호로 넘김 → MainWindow가 사용자 세션에 넣음
//   예약: 설정의 schedule 목록을 1초마다 확인 (요일·시각이 맞으면 한 번)
//   음성 팩(선택): <프로그램 폴더>\voice 에 dvbridge_tts.exe + onnxruntime.dll + supertonic3 모델이 있으면
//     자연스러운 목소리(Supertonic 3, 여성 F1~F5·남성 M1~M5)를 씀. 목소리 이름 "st:F1" 형식. 공지 만들 때만 잠깐 실행
//   설정(JSON, AppConfig::notice):
//     { "voice": "목소리 이름(비우면 한국어 자동)", "rate": -10~10, "chime": true,
//       "schedule": [ { "id": "...", "on": true, "time": "09:00", "days": [1..7](1=월), "text": "...",
//                       "targets": ["사용자 이름", ...](비우면 모두), "chime": true, "repeat": 1~3 } ] }
#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>

class QProcess;
class QTimer;

class NoticeService : public QObject
{
	Q_OBJECT
public:
	struct Job {
		QString text, source = "tts";
		QStringList targets;		// 사용자 이름 (비우면 모두)
		bool chime = true;
		int repeat = 1;
	};
	explicit NoticeService(QObject *parent = nullptr);
	~NoticeService() override;

	void set_config(const QJsonObject &c) { cfg_ = c; }
	const QJsonObject &config() const { return cfg_; }
	// 웹에서 받은 설정 검사·정리 (잘못되면 err에 이유, 빈 객체 반환)
	static QJsonObject sanitize(const QJsonObject &in, QString *err);

	void speak(const Job &j);					// 문장 읽기 (차례대로 만들어 ready)
	QJsonArray voices() const { return voices_; }	// [{name, culture}]
	bool tts_available() const { return voices_ok_ && !voices_.isEmpty(); }
	bool voicepack() const { return !pack_exe_.isEmpty(); }	// 음성 팩(자연스러운 목소리) 있음
	bool voicepack_usable() const { return voicepack() && cfg_.value("ai_agree").toBool(); }	// 사용 제한에 동의함
	static QString voicepack_dir();					// <프로그램 폴더>\voice
	int pending() const { return jobs_.size() + (proc_ ? 1 : 0); }
	QJsonArray recent() const { return recent_; }	// 최근 예약 실행 기록 (웹 표시)

	// 소리 도구 (시험에서도 사용)
	static QVector<int16_t> chime();				// 딩동 알림음 + 짧은 쉼
	static bool parse_wav(const QByteArray &wav, QVector<int16_t> &pcm, QString *err);	// 8kHz 16bit 모노만
	static QByteArray make_wav(const QVector<int16_t> &pcm);	// 8kHz 16bit 모노 WAV (웹 미리 듣기)
	static void normalize(QVector<int16_t> &pcm);	// 말소리 크기를 BM 음성과 비슷하게
	static QVector<int16_t> build(const QVector<int16_t> &speech, bool chime, int repeat);
	// 지금 시각에 울려야 할 예약 (now 기준, 이미 울린 것은 fired로 거름) - 시험용으로 공개
	static QList<QJsonObject> due(const QJsonArray &schedule, const QDateTime &now);
	// 무전 교신 말투로 바꾸기: 호출부호(예 HL0EXM) → 포네틱·글자, "TG 450" → 한 자리씩, 발음 사전(단어=읽는 법 줄마다)
	//   style: "phonetic"(식스 킬로 투…), "letters"(식스 케이 투…), "korean"(육 케이 이…)
	static QString radio_text(const QString &text, const QString &style, const QString &dict);
	static QString default_dict();
	QJsonObject status() const;					// 음성 만들기 진행 (웹 진행 표시)

signals:
	// ai = 음성 팩(Supertonic, AI 음성 모델)으로 만듦 → 휴대폰 이름표에 "(AI 음성)" 표시 (모델 라이선스 사용 제한 e)
	void ready(QVector<int16_t> pcm, QString title, QString source, QStringList targets, bool ai);
	void log(QString text);

private:
	void next_job();
	void tick();
	void load_voices();
	static QString powershell();
	QString pack_exe_, pack_model_;		// 음성 팩 실행 파일·모델 폴더 (없으면 빈 값)

	QJsonObject cfg_;
	QList<Job> jobs_;
	QProcess *proc_ = nullptr;
	Job cur_;
	QString cur_in_, cur_out_;
	bool cur_ai_ = false;
	QElapsedTimer job_clock_;
	qint64 job_estimate_ms_ = 0;
	double ms_per_char_st_ = 70, ms_per_char_win_ = 25;	// 걸린 시간으로 계속 맞춤 (예상 시간)
	QTimer *timer_ = nullptr;
	QSet<QString> fired_;			// "id|yyyyMMddHHmm"
	QJsonArray voices_;
	bool voices_ok_ = false;
	QJsonArray recent_;
};
