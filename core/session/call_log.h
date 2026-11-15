// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// CallLog — 송수신 이력 (BM Last Heard 형식에 맞춘 항목), 파일(JSON Lines)로 보관
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

struct CallRecord {
	qint64 start_ms = 0;		// 시작 시각 (Unix ms)
	qint64 duration_ms = 0;
	bool tx = false;			// true = 내가 송신, false = 수신
	quint32 src = 0, dst = 0;
	bool priv = false;
	int slot = 2;
	QString kind = "voice";		// "voice", "unlink", "notice"(브릿지 공지), "intercom"(내부 채널), "echo"(내부 에코) - BM과 무관
	QString src_call;			// 알면 콜사인 (내 송신은 내 콜사인)
	QString src_name;			// 알면 이름 (RadioID 명단)
	QString via;				// 이 기록을 남긴 사용자 브릿지 (다중 사용자, 보통 그 사용자의 콜사인)
	int ber_pct = -1;			// 수신 평균 BER(%) - 마스터가 주면
	QJsonObject to_json() const;
	static CallRecord from_json(const QJsonObject &o);
};

class CallLog : public QObject
{
	Q_OBJECT
public:
	explicit CallLog(QObject *parent = nullptr) : QObject(parent) {}
	void set_file(const QString &path, int keep = 500);	// 파일에서 최근 keep건 불러옴
	void add(const CallRecord &r);
	QJsonArray to_json(int limit = 200) const;			// 최신순
	int size() const { return records_.size(); }
	const QList<CallRecord> &records() const { return records_; }
signals:
	void added(CallRecord r);
private:
	QList<CallRecord> records_;		// 오래된 것 → 최신
	QString file_;
	int keep_ = 500;
};
