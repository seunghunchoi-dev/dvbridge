// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "app_settings.h"

#include <QJsonDocument>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace AppSettings {

namespace {
QString g_dir, g_note;

bool writable_dir(const QString &d)
{
	if (!QDir().mkpath(d)) return false;
	QFile f(QDir(d).filePath(".write_test"));
	if (!f.open(QIODevice::WriteOnly)) return false;
	f.close();
	f.remove();
	return true;
}

// 저장 폴더: 실행 파일 옆 data 폴더 (설치판·포터블판 공통)
//   - 시험용으로 DMRBRIDGE_CONFIG_DIR 환경변수가 있으면 그 폴더
//   - data 폴더에 쓸 수 없으면(권한 없음) %APPDATA%\DMRBridge 로 대신 저장
//   - 처음 실행할 때 예전 위치(%APPDATA%\DMRBridge)에 설정이 있으면 복사해 옴 (원본은 그대로)
void init_dir()
{
	if (!g_dir.isEmpty()) return;
	const QString env = qEnvironmentVariable("DMRBRIDGE_CONFIG_DIR");
	if (!env.isEmpty()) { QDir().mkpath(env); g_dir = env; return; }
	const QString old = QDir(qEnvironmentVariable("APPDATA")).filePath("DMRBridge");
	const QString data = QDir(QCoreApplication::applicationDirPath()).filePath("data");
	if (writable_dir(data)) {
		g_dir = data;
		if (!QFile::exists(QDir(data).filePath("dmrbridge.ini")) && QFile::exists(QDir(old).filePath("dmrbridge.ini"))) {
			for (const char *f : {"dmrbridge.ini", "history.jsonl", "user.csv"})
				QFile::copy(QDir(old).filePath(f), QDir(data).filePath(f));
			g_note = QString("예전 설정을 옮겨 왔습니다 (원본: %1)").arg(QDir::toNativeSeparators(old));
		}
	} else {
		QDir().mkpath(old);
		g_dir = old;
		g_note = QString("프로그램 폴더(%1)에 쓸 수 없어 %2 에 저장합니다").arg(QDir::toNativeSeparators(data), QDir::toNativeSeparators(old));
	}
}

constexpr int kKeepAuto = 20;	// 자동 백업 보관 개수
}

QString config_dir() { init_dir(); return g_dir; }
QString config_note() { init_dir(); return g_note; }
QString config_path() { return QDir(config_dir()).filePath("dmrbridge.ini"); }
QString backup_dir() { const QString d = QDir(config_dir()).filePath("backup"); QDir().mkpath(d); return d; }

QString backup_now(const QString &dest)
{
	const QString src = config_path();
	if (!QFile::exists(src)) return QString();
	QString to = dest;
	if (to.isEmpty()) to = QDir(backup_dir()).filePath("backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".ini");
	QFile::remove(to);
	return QFile::copy(src, to) ? to : QString();
}

// 저장한 결과를 자동 백업: 바뀐 내용만, 최근 kKeepAuto개 보관 (가장 최근 것 = 현재 설정)
static void auto_backup()
{
	const QString src = config_path();
	QFile in(src);
	if (!in.open(QIODevice::ReadOnly)) return;
	const QByteArray cur = in.readAll();
	in.close();
	QDir bd(backup_dir());
	const QStringList autos = bd.entryList(QStringList() << "auto-*.ini", QDir::Files, QDir::Name);
	if (!autos.isEmpty()) {
		QFile last(bd.filePath(autos.last()));
		if (last.open(QIODevice::ReadOnly) && last.readAll() == cur) return;
	}
	QFile::copy(src, bd.filePath("auto-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".ini"));
	QStringList all = bd.entryList(QStringList() << "auto-*.ini", QDir::Files, QDir::Name);
	while (all.size() > kKeepAuto) bd.remove(all.takeFirst());
}

bool check_backup(const QString &file, int *users, QString *err)
{
	QSettings s(file, QSettings::IniFormat);
	// 형식 오류만 거부 (방금 쓴 파일은 Qt가 접근 상태값을 남기는 경우가 있어 내용으로 판단)
	if (s.status() == QSettings::FormatError || !(s.childGroups().contains("users") || s.childGroups().contains("web"))) {
		if (err) *err = "DMRBridge 설정 파일이 아닙니다";
		return false;
	}
	if (users) { *users = s.beginReadArray("users"); s.endArray(); }
	return true;
}

bool restore_from(const QString &file, QString *err)
{
	if (!check_backup(file, nullptr, err)) return false;
	backup_now(QDir(backup_dir()).filePath("before-restore-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".ini"));
	const QString dst = config_path();
	if (QFileInfo(file).canonicalFilePath() == QFileInfo(dst).canonicalFilePath()) return true;
	// 파일을 통째로 바꾸지 않고 항목을 옮겨 씀 (Qt 설정 캐시와 어긋나지 않게)
	int want = 0;
	{
		QSettings in(file, QSettings::IniFormat), out(dst, QSettings::IniFormat);
		want = in.beginReadArray("users");
		in.endArray();
		out.clear();
		for (const QString &k : in.allKeys()) out.setValue(k, in.value(k));
		out.sync();
	}
	// 다시 읽어서 확인 (사용자 수가 같아야 성공)
	int got = -1;
	if (!check_backup(dst, &got, nullptr) || got != want) {
		if (err) *err = QString("설정 파일을 쓸 수 없습니다 (사용자 %1명 중 %2명 확인)").arg(want).arg(got);
		return false;
	}
	return true;
}

QString protect(const QString &plain)
{
	if (plain.isEmpty()) return QString();
#ifdef _WIN32
	const QByteArray in = plain.toUtf8();
	DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.constData()))};
	DATA_BLOB out{};
	if (!CryptProtectData(&src, L"DMRBridge", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return QString();
	const QByteArray enc(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
	LocalFree(out.pbData);
	return QString::fromLatin1(enc.toBase64());
#else
	return QString();
#endif
}

QString unprotect(const QString &b64)
{
	if (b64.isEmpty()) return QString();
#ifdef _WIN32
	const QByteArray enc = QByteArray::fromBase64(b64.toLatin1());
	DATA_BLOB src{DWORD(enc.size()), reinterpret_cast<BYTE *>(const_cast<char *>(enc.constData()))};
	DATA_BLOB out{};
	if (!CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return QString();
	const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
#else
	return QString();
#endif
}

int hotspot_suffix(const SessionConfig &c)
{
	if (c.dmr_id && c.master.id >= c.dmr_id * 100 && c.master.id < c.dmr_id * 100 + 100) return int(c.master.id - c.dmr_id * 100);
	return 0;
}

SessionConfig default_user()
{
	SessionConfig c;	// session.h 기본값: BM 4501, 9990 개인, TS2, CC1, USRP 32001, TOT 180초, 자동 Unlink
	return c;
}

static void read_user(QSettings &s, SessionConfig &c)
{
	c.enabled = s.value("enabled", true).toBool();
	c.master.host = s.value("host", "4501.master.brandmeister.network").toString();
	c.master.port = quint16(s.value("port", 62031).toUInt());
	c.master.callsign = s.value("callsign").toString();
	c.master.password = unprotect(s.value("password_dpapi").toString());
	c.master.options = s.value("options").toString();
	c.dmr_id = s.value("dmr_id", 0).toUInt();
	const int suffix = s.value("hotspot_suffix", 0).toInt();
	c.master.id = (c.dmr_id && suffix > 0) ? c.dmr_id * 100 + quint32(suffix) : c.dmr_id;
	c.tg = s.value("tg", 9990).toInt();
	c.priv = s.value("private", true).toBool();
	c.slot = s.value("slot", 2).toInt();
	c.cc = s.value("cc", 1).toInt();
	c.auto_unlink = s.value("auto_unlink", true).toBool();
	c.usrp_port = quint16(s.value("usrp_port", 32001).toUInt());
	c.tot_seconds = s.value("tot_seconds", 180).toInt();
	c.device = s.value("device").toString();
	c.name = c.master.callsign;
}

static void write_user(QSettings &s, const SessionConfig &c)
{
	s.setValue("enabled", c.enabled);
	s.setValue("host", c.master.host);
	s.setValue("port", c.master.port);
	s.setValue("callsign", c.master.callsign);
	s.setValue("password_dpapi", protect(c.master.password));	// 평문 저장 안 함
	s.setValue("options", c.master.options);
	s.setValue("dmr_id", c.dmr_id);
	s.setValue("hotspot_suffix", hotspot_suffix(c));
	s.setValue("tg", c.tg);
	s.setValue("private", c.priv);
	s.setValue("slot", c.slot);
	s.setValue("cc", c.cc);
	s.setValue("auto_unlink", c.auto_unlink);
	s.setValue("usrp_port", c.usrp_port);
	s.setValue("tot_seconds", c.tot_seconds);
	if (c.device.isEmpty()) s.remove("device"); else s.setValue("device", c.device);
}

AppConfig load()
{
	QSettings s(config_path(), QSettings::IniFormat);
	AppConfig a;
	a.web_port = quint16(s.value("web/port", 8080).toUInt());
	a.web_bind = s.value("web/bind", s.value("web/lan", true).toBool() ? "0.0.0.0" : "127.0.0.1").toString();	// 예전 web/lan → web/bind
	a.auto_service = s.value("service/auto_start", true).toBool();
	a.watchdog = s.value("service/watchdog", true).toBool();
	a.service_on = QSettings(QDir(config_dir()).filePath("state.ini"), QSettings::IniFormat).value("service/last_on", false).toBool();
	a.plugin_dir = s.value("vocoder/plugins").toString();
	a.admin_salt = s.value("web/admin_salt").toString();
	a.intercom_tg = s.value("intercom/tg", 99).toInt();
	a.notice = QJsonDocument::fromJson(s.value("notice/json").toString().toUtf8()).object();
	a.admin_hash = s.value("web/admin_hash").toString();
	const int n = s.beginReadArray("users");
	for (int i = 0; i < n; ++i) {
		s.setArrayIndex(i);
		SessionConfig c;
		read_user(s, c);
		a.users.append(c);
	}
	s.endArray();
	if (a.users.isEmpty() && s.contains("master/callsign")) {	// 예전 단일 사용자 형식 → 사용자 1
		SessionConfig c;
		c.master.host = s.value("master/host", c.master.host).toString();
		c.master.port = quint16(s.value("master/port", 62031).toUInt());
		c.master.callsign = s.value("master/callsign").toString();
		c.master.password = unprotect(s.value("master/password_dpapi").toString());
		c.master.options = s.value("master/options").toString();
		c.dmr_id = s.value("user/dmr_id", 0).toUInt();
		const int suffix = s.value("user/hotspot_suffix", 0).toInt();
		c.master.id = (c.dmr_id && suffix > 0) ? c.dmr_id * 100 + quint32(suffix) : c.dmr_id;
		c.tg = s.value("user/tg", 9990).toInt();
		c.priv = s.value("user/private", true).toBool();
		c.slot = s.value("user/slot", 2).toInt();
		c.cc = s.value("user/cc", 1).toInt();
		c.auto_unlink = s.value("user/auto_unlink", true).toBool();
		c.usrp_port = quint16(s.value("usrp/port", 32001).toUInt());
		c.tot_seconds = s.value("safety/tot_seconds", 180).toInt();
		c.name = c.master.callsign;
		a.users.append(c);
	}
	for (SessionConfig &c : a.users) c.plugin_dir = a.effective_plugin_dir();
	return a;
}

void save(const AppConfig &a)
{
	QSettings s(config_path(), QSettings::IniFormat);
	for (const char *g : {"master", "user", "usrp", "safety"}) s.remove(g);	// 예전 단일 사용자 형식 흔적 제거
	s.setValue("web/port", a.web_port);
	s.remove("web/lan");
	s.setValue("web/bind", a.web_bind);
	s.setValue("service/auto_start", a.auto_service);
	s.setValue("service/watchdog", a.watchdog);
	s.setValue("vocoder/plugins", a.plugin_dir);
	s.setValue("web/admin_salt", a.admin_salt);
	s.setValue("intercom/tg", a.intercom_tg);
	s.setValue("notice/json", QString::fromUtf8(QJsonDocument(a.notice).toJson(QJsonDocument::Compact)));
	s.setValue("web/admin_hash", a.admin_hash);
	s.remove("users");
	s.beginWriteArray("users", a.users.size());
	for (int i = 0; i < a.users.size(); ++i) {
		s.setArrayIndex(i);
		write_user(s, a.users[i]);
	}
	s.endArray();
	s.sync();
	auto_backup();
}

QJsonObject user_to_json(const SessionConfig &c)
{
	QJsonObject o;
	o["enabled"] = c.enabled;
	o["host"] = c.master.host;
	o["port"] = int(c.master.port);
	o["callsign"] = c.master.callsign;
	o["dmr_id"] = qint64(c.dmr_id);
	o["suffix"] = hotspot_suffix(c);
	o["hotspot_id"] = qint64(c.master.id);
	o["password_set"] = !c.master.password.isEmpty();	// 비밀번호 자체는 내보내지 않음
	o["options"] = c.master.options;
	o["tg"] = c.tg;
	o["priv"] = c.priv;
	o["slot"] = c.slot;
	o["cc"] = c.cc;
	o["auto_unlink"] = c.auto_unlink;
	o["usrp_port"] = int(c.usrp_port);
	o["tot_seconds"] = c.tot_seconds;
	o["device"] = c.device;		// 보기 전용 (클라이언트가 알려 준 장비명)
	return o;
}

bool user_from_json(const QJsonObject &o, SessionConfig &c, QString *err)
{
	auto fail = [err](const QString &m) { if (err) *err = m; return false; };
	const QString host = o["host"].toString().trimmed();
	const QString call = o["callsign"].toString().trimmed().toUpper();
	const qint64 id = o["dmr_id"].toVariant().toLongLong();
	const int suffix = o["suffix"].toInt(0);
	const int port = o["port"].toInt(62031), usrp = o["usrp_port"].toInt(32001);
	const int tg = o["tg"].toInt(9990), slot = o["slot"].toInt(2), cc = o["cc"].toInt(1), tot = o["tot_seconds"].toInt(180);
	if (host.isEmpty() || host.size() > 253) return fail("마스터 주소를 입력하세요");
	if (call.isEmpty() || call.size() > 12) return fail("콜사인을 입력하세요");
	if (id < 1000000 || id > 9999999) return fail("DMR ID는 7자리 숫자입니다");
	if (suffix < 0 || suffix > 99) return fail("핫스팟 접미사는 0~99입니다");
	if (port < 1 || port > 65535 || usrp < 1024 || usrp > 65535) return fail("포트 번호가 올바르지 않습니다");
	if (tg < 1 || tg > 16777215) return fail("토크그룹 번호가 올바르지 않습니다");
	if (slot < 1 || slot > 2 || cc < 0 || cc > 15 || tot < 10 || tot > 900) return fail("슬롯(1~2)·CC(0~15)·TOT(10~900초)를 확인하세요");
	const QString pw = o["password"].toString();
	if (pw.isEmpty() && c.master.password.isEmpty()) return fail("핫스팟 비밀번호를 입력하세요");
	c.enabled = o["enabled"].toBool(true);
	c.master.host = host;
	c.master.port = quint16(port);
	c.master.callsign = call;
	if (!pw.isEmpty()) c.master.password = pw;
	c.master.options = o["options"].toString().trimmed();
	c.dmr_id = quint32(id);
	c.master.id = suffix > 0 ? c.dmr_id * 100 + quint32(suffix) : c.dmr_id;
	c.tg = tg;
	c.priv = o["priv"].toBool(true);
	c.slot = slot;
	c.cc = cc;
	c.auto_unlink = o["auto_unlink"].toBool(true);
	c.usrp_port = quint16(usrp);
	c.tot_seconds = tot;
	c.name = call;
	return true;
}

static QByteArray admin_digest(const QString &pw, const QByteArray &salt)
{
	return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, pw.toUtf8(), salt, 120000, 32);
}

bool has_admin_password(const AppConfig &a) { return !a.admin_hash.isEmpty() && !a.admin_salt.isEmpty(); }

void set_admin_password(AppConfig &a, const QString &pw)
{
	QByteArray salt(16, '\0');
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), 4);
	a.admin_salt = QString::fromLatin1(salt.toBase64());
	a.admin_hash = QString::fromLatin1(admin_digest(pw, salt).toBase64());
}

bool check_admin_password(const AppConfig &a, const QString &pw)
{
	if (!has_admin_password(a)) return false;
	const QByteArray want = QByteArray::fromBase64(a.admin_hash.toLatin1());
	const QByteArray got = admin_digest(pw, QByteArray::fromBase64(a.admin_salt.toLatin1()));
	if (want.size() != got.size()) return false;
	unsigned char diff = 0;	// 비교 시간이 내용에 따라 달라지지 않게
	for (int i = 0; i < got.size(); ++i) diff |= uchar(want[i] ^ got[i]);
	return diff == 0;
}

void save_service_state(bool on)
{
	QSettings st(QDir(config_dir()).filePath("state.ini"), QSettings::IniFormat);	// 자주 바뀌므로 설정 파일·자동 백업과 분리
	st.setValue("service/last_on", on);
	st.sync();
}

QStringList validate(const AppConfig &a)
{
	QStringList err;
	QHash<quint16, QString> ports;
	QHash<quint32, QString> ids;
	for (const SessionConfig &c : a.users) {
		if (!c.enabled) continue;
		const QString who = c.master.callsign.isEmpty() ? QString("(콜사인 없음)") : c.master.callsign;
		if (c.master.callsign.isEmpty() || c.dmr_id < 1000000 || c.master.password.isEmpty()) err << who + ": 콜사인·DMR ID·비밀번호를 입력하세요";
		if (ports.contains(c.usrp_port)) err << QString("USRP 포트 %1이 %2와 %3에서 겹침 - 사용자마다 다른 포트(휴대폰 앱의 TX Port)가 필요").arg(c.usrp_port).arg(ports[c.usrp_port], who);
		else ports.insert(c.usrp_port, who);
		if (ids.contains(c.master.id)) err << QString("핫스팟 ID %1이 %2와 %3에서 겹침 - 같은 ID로 두 번 접속하면 BM이 앞 접속을 끊음 (접미사를 다르게)").arg(c.master.id).arg(ids[c.master.id], who);
		else ids.insert(c.master.id, who);
		if (c.usrp_port == a.web_port) err << QString("%1: USRP 포트가 대시보드 포트와 같음").arg(who);
	}
	return err;
}

} // namespace AppSettings

QString AppConfig::effective_plugin_dir() const
{
	return plugin_dir.isEmpty() ? QCoreApplication::applicationDirPath() + "/plugins" : plugin_dir;
}

