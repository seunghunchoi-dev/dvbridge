// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// IdDatabase — DMR ID → 콜사인·이름 (RadioID 공개 명단 user.csv)
//   형식: RADIO_ID,CALLSIGN,FIRST_NAME,LAST_NAME,CITY,STATE,COUNTRY (첫 줄 머리글)
#pragma once
#include <QDateTime>
#include <QHash>
#include <QString>

class IdDatabase
{
public:
	struct Info { QString call, name, loc; };	// loc: "도시, 나라"
	bool load(const QString &csv_path);		// 성공하면 true (머리글이 다르거나 0건이면 false, 기존 내용 유지)
	bool load_data(const QByteArray &csv);	// 메모리의 CSV
	const Info *find(quint32 id) const { auto it = map_.constFind(id); return it == map_.constEnd() ? nullptr : &it.value(); }
	int size() const { return map_.size(); }
	QDateTime file_time() const { return file_time_; }
	// BM 형식 표시: "HL0ABC [Hong] (4500001)", 모르면 "4500001"
	QString display(quint32 id) const;
private:
	QHash<quint32, Info> map_;
	QDateTime file_time_;
};
