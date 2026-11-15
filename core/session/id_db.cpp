// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "id_db.h"

#include <QFile>
#include <QFileInfo>

// 큰따옴표로 감싼 칸(쉼표 포함 가능)도 처리하는 간단한 CSV 한 줄 분리
static QList<QByteArray> split_csv(const QByteArray &line)
{
	QList<QByteArray> out;
	QByteArray cur;
	bool q = false;
	for (int i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (q) {
			if (c == '"') { if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; } else q = false; }
			else cur += c;
		}
		else if (c == '"') q = true;
		else if (c == ',') { out.append(cur); cur.clear(); }
		else if (c != '\r' && c != '\n') cur += c;
	}
	out.append(cur);
	return out;
}

bool IdDatabase::load_data(const QByteArray &csv)
{
	if (!csv.startsWith("RADIO_ID,CALLSIGN")) return false;
	QHash<quint32, Info> m;
	m.reserve(400000);
	int pos = csv.indexOf('\n');
	while (pos >= 0 && pos < csv.size()) {
		const int next = csv.indexOf('\n', pos + 1);
		const QByteArray line = csv.mid(pos + 1, (next < 0 ? csv.size() : next) - pos - 1);
		pos = next;
		if (line.size() < 3) continue;
		const QList<QByteArray> f = split_csv(line);
		if (f.size() < 3) continue;
		bool ok = false;
		const quint32 id = f[0].toUInt(&ok);
		if (!ok || !id || id > 0xFFFFFF) continue;
		Info inf;
		inf.call = QString::fromUtf8(f[1]).trimmed().toUpper();
		inf.name = QString::fromUtf8(f[2]).trimmed().section(' ', 0, 0);	// 이름 첫 단어 (BM 표시와 비슷하게)
		if (inf.call.isEmpty()) continue;
		if (f.size() >= 7) {	// 지역: 도시, 나라 (상대방 정보 표시용)
			const QString city = QString::fromUtf8(f[4]).trimmed(), country = QString::fromUtf8(f[6]).trimmed();
			inf.loc = city.isEmpty() ? country : (country.isEmpty() ? city : city + ", " + country);
		}
		m.insert(id, inf);
	}
	if (m.isEmpty()) return false;
	map_.swap(m);
	return true;
}

bool IdDatabase::load(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return false;
	if (!load_data(f.readAll())) return false;
	file_time_ = QFileInfo(path).lastModified();
	return true;
}

QString IdDatabase::display(quint32 id) const
{
	const Info *i = find(id);
	if (!i) return QString::number(id);
	return i->name.isEmpty() ? QString("%1 (%2)").arg(i->call).arg(id) : QString("%1 [%2] (%3)").arg(i->call, i->name).arg(id);
}
