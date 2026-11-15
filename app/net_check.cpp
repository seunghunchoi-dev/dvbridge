// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "net_check.h"

#include <QDateTime>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUdpSocket>

namespace NetKind {

static QHostAddress plain(const QHostAddress &a)
{
	bool ok = false;
	const quint32 v4 = a.toIPv4Address(&ok);	// ::ffff:1.2.3.4 → 1.2.3.4
	return ok ? QHostAddress(v4) : a;
}

QString of(const QHostAddress &addr)
{
	const QHostAddress a = plain(addr);
	if (a.isNull()) return QString();
	if (a.isLoopback()) return "이 PC";
	if (a.protocol() == QAbstractSocket::IPv4Protocol) {
		const quint32 v = a.toIPv4Address();
		if ((v >> 24) == 10 || (v >> 20) == (172u << 4 | 1) || (v >> 16) == (192u << 8 | 168)) return "내부망";
		if ((v >> 22) == ((100u << 2) | 1)) return "내부망";	// 100.64.0.0/10 통신사 공유 주소
		if ((v >> 16) == (169u << 8 | 254)) return "링크로컬";
		return "외부망";
	}
	const Q_IPV6ADDR b = a.toIPv6Address();
	if ((b[0] & 0xFE) == 0xFC) return "내부망";
	if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return "링크로컬";
	return "외부망";
}

bool is_public(const QHostAddress &a) { return of(a) == "외부망"; }

QString of_text(const QString &s)
{
	QString h = s.trimmed();
	if (h.isEmpty()) return QString();
	if (h.count(':') == 1) h = h.section(':', 0, 0);		// IPv4:포트
	if (h.startsWith('[')) h = h.mid(1, h.indexOf(']') - 1);	// [IPv6]:포트
	return of(QHostAddress(h));
}
}

NetCheck::NetCheck(QObject *parent) : QObject(parent)
{
	nam_ = new QNetworkAccessManager(this);
	timeout_ = new QTimer(this);
	timeout_->setSingleShot(true);
	connect(timeout_, &QTimer::timeout, this, [this] { if (pending_ > 0) { pending_ = 0; finish(); } });
}

void NetCheck::start(const Input &in, bool lookup_public_ip)
{
	if (running()) return;
	in_ = in;
	res_ = QJsonObject();
	public_ip_.clear();
	res_["checked_at"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
	res_["running"] = true;

	// 이 PC의 주소
	QJsonArray ifs;
	for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
		if (!(ni.flags() & QNetworkInterface::IsUp) || !(ni.flags() & QNetworkInterface::IsRunning) || (ni.flags() & QNetworkInterface::IsLoopBack)) continue;
		for (const QNetworkAddressEntry &e : ni.addressEntries()) {
			const QHostAddress a = e.ip();
			if (a.protocol() != QAbstractSocket::IPv4Protocol || NetKind::of(a) == "링크로컬") continue;
			ifs.append(QJsonObject{{"name", ni.humanReadableName()}, {"ip", a.toString()}, {"kind", NetKind::of(a)}});
		}
	}
	res_["interfaces"] = ifs;

	// 인터넷으로 나가는 쪽 주소 (UDP connect는 패킷을 보내지 않고 경로만 정함)
	{
		QUdpSocket u;
		u.connectToHost(QHostAddress("1.1.1.1"), 53);
		u.waitForConnected(300);
		outbound_ip_ = u.localAddress().toString();
	}
	res_["outbound_ip"] = outbound_ip_;
	res_["outbound_kind"] = NetKind::of(QHostAddress(outbound_ip_));

	// 포트·휴대폰
	res_["web"] = QJsonObject{{"bind", in_.web_bind}, {"port", int(in_.web_port)}, {"listening", in_.web_listening}};
	QJsonArray users;
	for (const QJsonValue &v : in_.users) {
		QJsonObject u = v.toObject();
		u["client_net"] = NetKind::of_text(u["client"].toString());
		users.append(u);
	}
	res_["users"] = users;

	pending_ = 0;
	// ① BM 마스터 이름 확인 (DNS)
	QSet<QString> hosts(in_.master_hosts.begin(), in_.master_hosts.end());
	res_["dns"] = QJsonArray();
	for (const QString &h : hosts) {
		++pending_;
		QHostInfo::lookupHost(h, this, [this, h](const QHostInfo &hi) {
			QStringList ips;
			for (const QHostAddress &a : hi.addresses()) ips << a.toString();
			QJsonArray arr = res_["dns"].toArray();
			arr.append(QJsonObject{{"host", h}, {"ok", hi.error() == QHostInfo::NoError && !ips.isEmpty()}, {"ips", ips.join(", ")}});
			res_["dns"] = arr;
			done_one();
		});
	}
	// ② RadioID 접속 (HTTPS)
	{
		++pending_;
		QNetworkRequest rq(QUrl("https://radioid.net/"));
		rq.setHeader(QNetworkRequest::UserAgentHeader, "DVBridge");
		rq.setTransferTimeout(8000);
		QNetworkReply *r = nam_->head(rq);
		connect(r, &QNetworkReply::finished, this, [this, r] {
			const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			res_["radioid"] = QJsonObject{{"ok", r->error() == QNetworkReply::NoError || (code > 0 && code < 500)}, {"detail", code ? QString("HTTP %1").arg(code) : r->errorString()}};
			r->deleteLater();
			done_one();
		});
	}
	// ③ (선택) 공인 IP — 외부 서비스(api.ipify.org)에 물어봄. 사용자가 버튼을 눌렀을 때만
	if (lookup_public_ip) {
		++pending_;
		QNetworkRequest rq(QUrl("https://api.ipify.org"));
		rq.setHeader(QNetworkRequest::UserAgentHeader, "DVBridge");
		rq.setTransferTimeout(8000);
		QNetworkReply *r = nam_->get(rq);
		connect(r, &QNetworkReply::finished, this, [this, r] {
			const QString ip = QString::fromLatin1(r->readAll()).trimmed();
			if (r->error() == QNetworkReply::NoError && !QHostAddress(ip).isNull()) public_ip_ = ip;
			res_["public_ip"] = public_ip_;
			res_["public_ip_error"] = public_ip_.isEmpty() ? r->errorString() : QString();
			r->deleteLater();
			done_one();
		});
	}
	// ④ Windows 방화벽·네트워크 종류 (읽기만)
	{
		++pending_;
		QString exe = in_.exe_path;
		exe.replace('\'', "''");
		// Windows 방화벽 COM(HNetCfg.FwPolicy2)으로 읽기만 함 (Get-NetFirewallApplicationFilter는 규칙이 많으면 수십 초 걸림)
		const QString script =
			"[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
			"$fw=New-Object -ComObject HNetCfg.FwPolicy2;"
			"foreach($k in @(@('Domain',1),@('Private',2),@('Public',4))){ 'FW=' + $k[0] + '=' + $fw.FirewallEnabled($k[1]) };"
			"Get-NetConnectionProfile | ForEach-Object { 'CAT=' + $_.InterfaceAlias + '=' + $_.NetworkCategory };"
			"$fw.Rules | Where-Object { $_.ApplicationName -ieq '" + exe + "' -and $_.Direction -eq 1 -and $_.Enabled } | "
			"ForEach-Object { 'RULE=' + $(if ($_.Action -eq 1) { 'Allow' } else { 'Block' }) + '=' + $_.Profiles }";
		auto *p = new QProcess(this);
		connect(p, &QProcess::finished, this, [this, p] {
			QJsonArray profiles, cats, rules;
			for (const QString &line : QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
				const QStringList f = line.trimmed().split('=');
				if (f.size() < 3) continue;
				if (f[0] == "FW") profiles.append(QJsonObject{{"name", f[1]}, {"enabled", f[2] == "True"}});
				else if (f[0] == "CAT") cats.append(QJsonObject{{"iface", f[1]}, {"category", f[2]}});
				else if (f[0] == "RULE") rules.append(QJsonObject{{"action", f[1]}, {"profile", f[2]}});
			}
			res_["firewall"] = QJsonObject{{"profiles", profiles}, {"networks", cats}, {"rules", rules}, {"read", !profiles.isEmpty()}};
			p->deleteLater();
			done_one();
		});
		const QString ps = qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
		connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			res_["firewall"] = QJsonObject{{"read", false}};
			p->deleteLater();
			done_one();
		});
		p->start(ps, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script});
	}
	timeout_->start(20000);
}

void NetCheck::done_one()
{
	if (pending_ <= 0) return;
	if (--pending_ == 0) finish();
}

void NetCheck::finish()
{
	timeout_->stop();
	build_advice();
	res_["running"] = false;
	emit finished();
}

void NetCheck::build_advice()
{
	QJsonArray adv;
	auto add = [&](const char *level, const QString &t) { adv.append(QJsonObject{{"level", level}, {"text", t}}); };
	QStringList lan, pub;
	for (const QJsonValue &v : res_["interfaces"].toArray()) (v.toObject()["kind"].toString() == "외부망" ? pub : lan) << v.toObject()["ip"].toString();
	QStringList usrp;
	int running = 0, connected = 0, ext_phone = 0;
	for (const QJsonValue &v : res_["users"].toArray()) {
		const QJsonObject u = v.toObject();
		usrp << QString::number(u["usrp_port"].toInt());
		running += u["running"].toBool();
		connected += u["connected"].toBool();
		if (!u["client"].toString().isEmpty())
			add(u["client_net"].toString() == "외부망" ? "ok" : "info", QString("%1: 휴대폰 %2 (%3)에서 접속 중").arg(u["call"].toString(), u["client"].toString(), u["client_net"].toString()));
		ext_phone += u["client_net"].toString() == "외부망";
	}
	if (ext_phone) add("ok", "외부망에서 휴대폰이 접속해 있습니다 — 외부 접속이 실제로 되는 것이 확인됐습니다.");
	const QString usrp_list = usrp.isEmpty() ? QString("(사용자 없음)") : usrp.join(", ");
	const bool web_public = in_.web_listening && (in_.web_bind == "0.0.0.0" || NetKind::is_public(QHostAddress(in_.web_bind)));
	const QString outk = res_["outbound_kind"].toString();

	if (outk == "외부망") {
		add("ok", QString("이 PC는 공인 IP %1 로 인터넷에 바로 연결되어 있습니다 (공유기 없이). 외부망(LTE·5G) 휴대폰은 Hostname에 %1 을 넣으면 접속할 수 있습니다. "
						  "통신사가 포트를 막는지는 밖에서 한 번 접속해 보아야 확실합니다.").arg(outbound_ip_));
		if (web_public) {
			add("warn", QString("웹 대시보드(포트 %1)가 인터넷 전체에 열려 있습니다. 누구나 대시보드를 보고 실시간으로 들을 수 있습니다.").arg(in_.web_port));
			if (!in_.admin_password) add("warn", "관리자 비밀번호가 아직 없습니다. 웹 설정에서 먼저 정하세요 (처음 정하기는 이 PC에서만 됩니다).");
			if (!lan.isEmpty()) add("info", QString("집 안에서만 쓰려면 DVBridge 프로그램의 [웹 접속 주소]를 '이 주소만: %1'로 바꾸세요.").arg(lan.first()));
		}
	} else if (outk == "내부망") {
		add("info", QString("이 PC는 공유기 뒤의 내부망 주소 %1 로 인터넷에 나갑니다. 같은 와이파이의 휴대폰은 Hostname에 %1 을 넣으면 됩니다.").arg(outbound_ip_));
		if (!public_ip_.isEmpty()) {
			add("info", QString("공인 IP: %1").arg(public_ip_));
			add("warn", QString("외부망(LTE·5G) 휴대폰에서 쓰려면 공유기 설정에서 포트포워딩이 필요합니다: UDP %1 → %2 (같은 포트). "
								"그다음 휴대폰 Hostname에 %3 을 넣으세요.").arg(usrp_list, outbound_ip_, public_ip_));
			add("info", "공유기의 인터넷(WAN) 주소가 위 공인 IP와 다르면 통신사 공유 IP라서 포트포워딩으로도 외부 접속이 안 될 수 있습니다.");
		} else {
			add("info", "외부망에서 쓸 수 있는지 보려면 [공인 IP도 확인]을 눌러 주세요.");
		}
	} else {
		add("warn", "인터넷으로 나가는 경로를 찾지 못했습니다. 네트워크 연결을 확인하세요.");
	}
	if (!pub.isEmpty() && !lan.isEmpty()) add("info", QString("이 PC에는 내부망(%1)과 외부망(%2) 주소가 모두 있습니다.").arg(lan.join(", "), pub.join(", ")));

	// 포트
	if (!in_.web_listening) add("warn", QString("웹 화면이 열려 있지 않습니다 (포트 %1). 프로그램의 [웹 접속 주소]를 확인하세요.").arg(in_.web_port));
	if (running) add("ok", QString("USRP 포트 열림: UDP %1").arg(usrp_list));
	else add("info", "서비스가 꺼져 있어 USRP 포트가 닫혀 있습니다. 휴대폰을 연결하려면 [서비스 켜기].");

	// 방화벽 (읽기만)
	const QJsonObject fw = res_["firewall"].toObject();
	if (fw["read"].toBool()) {
		QSet<QString> active;		// 연결된 네트워크의 종류 → 적용되는 방화벽 프로필
		for (const QJsonValue &v : fw["networks"].toArray()) {
			const QString c = v.toObject()["category"].toString();
			active << (c == "DomainAuthenticated" ? "Domain" : c);
		}
		bool on = false;
		for (const QJsonValue &v : fw["profiles"].toArray())
			if (v.toObject()["enabled"].toBool() && (active.isEmpty() || active.contains(v.toObject()["name"].toString()))) on = true;
		bool allow = false, block = false;
		for (const QJsonValue &v : fw["rules"].toArray()) (v.toObject()["action"].toString() == "Allow" ? allow : block) = true;
		if (!on) add("info", "Windows 방화벽이 꺼져 있어 들어오는 연결을 막지 않습니다 (다른 보안 프로그램이 막을 수는 있습니다).");
		else if (block && !allow) add("warn", "Windows 방화벽에 DVBridge 차단 규칙이 있습니다. 휴대폰 접속이 막힙니다. 'Windows 보안 → 방화벽 및 네트워크 보호 → 방화벽에서 앱 허용'에서 DVBridge를 허용하세요.");
		else if (!allow) add("warn", "Windows 방화벽이 켜져 있는데 DVBridge 허용 규칙이 없습니다. 휴대폰·다른 PC 접속이 막힐 수 있습니다. 'Windows 보안 → 방화벽에서 앱 허용'에서 DVBridge를 허용하세요.");
		else add("ok", "Windows 방화벽에 DVBridge 허용 규칙이 있습니다.");
	} else {
		add("info", "Windows 방화벽 상태를 읽지 못했습니다.");
	}

	// 인터넷
	for (const QJsonValue &v : res_["dns"].toArray()) {
		const QJsonObject d = v.toObject();
		if (d["ok"].toBool()) add("ok", QString("BM 마스터 %1 주소 확인 (%2)").arg(d["host"].toString(), d["ips"].toString()));
		else add("warn", QString("BM 마스터 %1 의 주소를 찾지 못했습니다. 인터넷 연결이나 DNS를 확인하세요.").arg(d["host"].toString()));
	}
	if (running) add(connected == running ? "ok" : "warn", QString("BM 접속 %1/%2").arg(connected).arg(running));
	const QJsonObject rid = res_["radioid"].toObject();
	if (!rid.isEmpty()) add(rid["ok"].toBool() ? "ok" : "info", rid["ok"].toBool() ? QString("RadioID 접속 가능 (ID 명단 받기)") : QString("RadioID에 접속하지 못했습니다: %1").arg(rid["detail"].toString()));
	if (res_.contains("public_ip") && public_ip_.isEmpty()) add("info", "공인 IP를 조회하지 못했습니다: " + res_["public_ip_error"].toString());
	res_["advice"] = adv;
}
