// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "id_db_updater.h"
#include "session/id_db.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

IdDbUpdater::IdDbUpdater(QObject *parent) : QObject(parent) {}

void IdDbUpdater::start(const QString &dest, const QString &url)
{
	if (reply_) return;
	if (!nam_) nam_ = new QNetworkAccessManager(this);
	dest_ = dest;
	part_ = new QFile(dest + ".part", this);
	if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		emit finished(false, "임시 파일을 만들 수 없음: " + part_->fileName());
		delete part_; part_ = nullptr;
		return;
	}
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, "DVBridge");	// 개인정보 없이 프로그램 이름만
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(60000);
	reply_ = nam_->get(req);
	connect(reply_, &QNetworkReply::readyRead, this, [this] { part_->write(reply_->readAll()); });
	connect(reply_, &QNetworkReply::downloadProgress, this, &IdDbUpdater::progress);
	connect(reply_, &QNetworkReply::finished, this, &IdDbUpdater::done);
}

void IdDbUpdater::cancel()
{
	if (reply_) reply_->abort();
}

void IdDbUpdater::done()
{
	QNetworkReply *r = reply_;
	reply_ = nullptr;
	part_->write(r->readAll());
	part_->close();
	const bool net_ok = r->error() == QNetworkReply::NoError;
	const QString err = r->errorString();
	r->deleteLater();
	if (!net_ok) {
		part_->remove();
		delete part_; part_ = nullptr;
		emit finished(false, "내려받기 실패: " + err);
		return;
	}
	IdDatabase check;
	if (!check.load(part_->fileName())) {
		part_->remove();
		delete part_; part_ = nullptr;
		emit finished(false, "받은 파일이 RadioID 명단 형식이 아님 - 기존 명단 유지");
		return;
	}
	QFile::remove(dest_);
	const bool ok = part_->rename(dest_);
	delete part_; part_ = nullptr;
	emit finished(ok, ok ? QString("명단 %1명 받음").arg(check.size()) : QString("파일 교체 실패: ") + dest_);
}
