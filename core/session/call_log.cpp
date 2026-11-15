// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "call_log.h"

#include <QFile>
#include <QJsonDocument>

QJsonObject CallRecord::to_json() const
{
	QJsonObject o;
	o["start"] = double(start_ms);
	o["dur"] = double(duration_ms);
	o["dir"] = tx ? "TX" : "RX";
	o["src"] = double(src);
	o["dst"] = double(dst);
	o["priv"] = priv;
	o["slot"] = slot;
	o["kind"] = kind;
	if (!src_call.isEmpty()) o["call"] = src_call;
	if (!src_name.isEmpty()) o["name"] = src_name;
	if (!via.isEmpty()) o["via"] = via;
	if (ber_pct >= 0) o["ber"] = ber_pct;
	return o;
}

CallRecord CallRecord::from_json(const QJsonObject &o)
{
	CallRecord r;
	r.start_ms = qint64(o["start"].toDouble());
	r.duration_ms = qint64(o["dur"].toDouble());
	r.tx = o["dir"].toString() == "TX";
	r.src = quint32(o["src"].toDouble());
	r.dst = quint32(o["dst"].toDouble());
	r.priv = o["priv"].toBool();
	r.slot = o["slot"].toInt(2);
	r.kind = o["kind"].toString("voice");
	r.src_call = o["call"].toString();
	r.src_name = o["name"].toString();
	r.via = o["via"].toString();
	r.ber_pct = o.contains("ber") ? o["ber"].toInt() : -1;
	return r;
}

void CallLog::set_file(const QString &path, int keep)
{
	file_ = path;
	keep_ = keep;
	records_.clear();
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
	while (!f.atEnd()) {
		const QJsonDocument d = QJsonDocument::fromJson(f.readLine());
		if (d.isObject()) records_.append(CallRecord::from_json(d.object()));
	}
	while (records_.size() > keep_) records_.removeFirst();
}

void CallLog::add(const CallRecord &r)
{
	records_.append(r);
	while (records_.size() > keep_) records_.removeFirst();
	if (!file_.isEmpty()) {
		QFile f(file_);
		if (f.open(QIODevice::Append | QIODevice::Text))
			f.write(QJsonDocument(r.to_json()).toJson(QJsonDocument::Compact) + "\n");
	}
	emit added(r);
}

QJsonArray CallLog::to_json(int limit) const
{
	QJsonArray a;
	for (int i = records_.size() - 1; i >= 0 && a.size() < limit; --i) a.append(records_[i].to_json());
	return a;
}
