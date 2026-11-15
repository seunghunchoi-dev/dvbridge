// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// IdDbUpdater — RadioID 공개 명단(user.csv, 약 17MB) 내려받기
//   임시 파일(.part)로 받은 뒤 내용을 확인하고 나서 교체 → 실패해도 기존 명단 유지
#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class IdDbUpdater : public QObject
{
	Q_OBJECT
public:
	static constexpr const char *kDefaultUrl = "https://radioid.net/static/user.csv";
	explicit IdDbUpdater(QObject *parent = nullptr);
	void start(const QString &dest_path, const QString &url = QString::fromLatin1(kDefaultUrl));
	bool busy() const { return reply_ != nullptr; }
	void cancel();
signals:
	void progress(qint64 received, qint64 total);
	void finished(bool ok, QString message);
private:
	void done();
	QNetworkAccessManager *nam_ = nullptr;
	QNetworkReply *reply_ = nullptr;
	QFile *part_ = nullptr;
	QString dest_;
};
