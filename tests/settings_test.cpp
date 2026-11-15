// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// 설정(다중 사용자) 저장·불러오기, 예전 형식 이전, DPAPI, 겹침 검사 (임시 폴더 사용, 실제 설정 파일은 건드리지 않음)
#include <cstdio>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include "app_settings.h"
#include "net_check.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	QTemporaryDir tmp;
	qputenv("DMRBRIDGE_CONFIG_DIR", tmp.path().toLocal8Bit());

	const QString enc = AppSettings::protect("--비밀#123");
	CHECK(!enc.isEmpty() && !enc.contains("비밀"), "DPAPI 암호화 결과에 평문 없음");
	CHECK(AppSettings::unprotect(enc) == "--비밀#123", "DPAPI 복호화 = 원래 값");
	CHECK(AppSettings::unprotect("AAAA").isEmpty(), "엉뚱한 값은 복호화 실패(빈 값)");

	CHECK(AppSettings::load().users.isEmpty(), "처음 실행: 사용자 없음");

	{	// 예전 단일 사용자 형식 파일 → 사용자 1로 이전
		QSettings s(AppSettings::config_path(), QSettings::IniFormat);
		s.setValue("master/callsign", "HL0EXM");
		s.setValue("master/password_dpapi", AppSettings::protect("oldpw"));
		s.setValue("user/dmr_id", 4500001);
		s.setValue("user/hotspot_suffix", 99);
		s.setValue("user/tg", 450);
		s.setValue("usrp/port", 32001);
		s.setValue("safety/min_gap_ms", 2000);
		s.sync();
	}
	AppConfig a = AppSettings::load();
	CHECK(a.users.size() == 1 && a.users[0].master.callsign == "HL0EXM" && a.users[0].master.id == 450000199
		  && a.users[0].master.password == "oldpw" && a.users[0].tg == 450, "예전 형식 → 사용자 1로 이전");

	SessionConfig b = AppSettings::default_user();
	b.master.callsign = "HL1ABC"; b.dmr_id = 4501234; b.master.id = 450123401; b.master.password = "pw2"; b.usrp_port = 32002;
	a.users.append(b);
	a.web_port = 8081;
	AppSettings::save(a);
	QFile f(AppSettings::config_path());
	f.open(QIODevice::ReadOnly);
	const QByteArray raw = f.readAll();
	CHECK(!raw.contains("oldpw") && !raw.contains("pw2"), "설정 파일에 비밀번호 평문 없음");
	CHECK(!raw.contains("[master]") && !raw.contains("min_gap"), "예전 형식 흔적 제거");
	const AppConfig r = AppSettings::load();
	CHECK(r.users.size() == 2 && r.users[1].master.callsign == "HL1ABC" && r.users[1].master.password == "pw2"
		  && r.users[1].usrp_port == 32002 && r.web_port == 8081, "사용자 2명·공통 설정 저장/복원");
	CHECK(AppSettings::hotspot_suffix(r.users[1]) == 1, "핫스팟 접미사 01 복원");
	CHECK(AppSettings::validate(r).isEmpty(), "겹침 없음 → 문제 없음");

	AppConfig bad = r;
	bad.users[1].usrp_port = 32001;
	CHECK(AppSettings::validate(bad).join("").contains("USRP 포트 32001"), "USRP 포트 겹침 감지");
	bad = r;
	bad.users[1].master.id = 450000199;
	CHECK(AppSettings::validate(bad).join("").contains("핫스팟 ID 450000199"), "핫스팟 ID 겹침 감지");
	bad.users[1].enabled = false;
	CHECK(AppSettings::validate(bad).isEmpty(), "사용 안 하는 사용자는 겹침 검사에서 제외");

	// ---- 내부망·외부망 구분 ----
	CHECK(NetKind::of_text("172.20.1.41:32002") == "내부망" && NetKind::of_text("192.168.0.10") == "내부망" && NetKind::of_text("10.1.2.3:5") == "내부망",
		  "사설 주소(10/172.16-31/192.168, 포트 붙은 문자열 포함) = 내부망");
	CHECK(NetKind::of_text("172.32.0.1") == "외부망" && NetKind::of_text("203.0.113.25:32001") == "외부망" && NetKind::of_text("8.8.8.8") == "외부망",
		  "공인 주소 = 외부망 (172.32는 사설 아님)");
	CHECK(NetKind::of_text("100.64.1.1") == "내부망" && NetKind::of_text("100.128.0.1") == "외부망", "통신사 공유 주소 100.64/10 = 내부망");
	CHECK(NetKind::of_text("169.254.3.4") == "링크로컬" && NetKind::of_text("127.0.0.1:80") == "이 PC" && NetKind::of_text("::ffff:192.168.1.5") == "내부망",
		  "링크로컬·이 PC·IPv4 매핑 주소");
	CHECK(NetKind::of_text("").isEmpty(), "빈 주소 = 빈 값");

	// ---- 웹 설정용 변환 · 관리자 비밀번호 ----
	{
		const QJsonObject j = AppSettings::user_to_json(r.users[1]);
		CHECK(!j.contains("password") && j["password_set"].toBool() && !QJsonDocument(j).toJson().contains("pw2"), "웹으로 내보낼 때 핫스팟 비밀번호 없음 (password_set만)");
		SessionConfig c = r.users[1];
		QJsonObject in = j;
		in["tg"] = 450;
		QString err;
		CHECK(AppSettings::user_from_json(in, c, &err) && c.master.password == "pw2" && c.tg == 450, "비밀번호 칸이 비면 기존 비밀번호 유지");
		in["password"] = "newpw";
		CHECK(AppSettings::user_from_json(in, c, &err) && c.master.password == "newpw", "비밀번호를 입력하면 바뀜");
		QJsonObject badj = in; badj["dmr_id"] = 123;
		CHECK(!AppSettings::user_from_json(badj, c, &err) && err.contains("7자리"), "잘못된 DMR ID 거부");
		badj = in; badj["usrp_port"] = 80;
		CHECK(!AppSettings::user_from_json(badj, c, &err), "잘못된 포트 거부");
		SessionConfig fresh;
		badj = in; badj.remove("password");
		CHECK(!AppSettings::user_from_json(badj, fresh, &err) && err.contains("비밀번호"), "새 사용자는 비밀번호 필수");
		AppConfig a2 = r;
		CHECK(!AppSettings::has_admin_password(a2), "관리자 비밀번호 처음엔 없음");
		AppSettings::set_admin_password(a2, "관리자pw1");
		CHECK(AppSettings::has_admin_password(a2) && !a2.admin_hash.contains("관리자") && AppSettings::check_admin_password(a2, "관리자pw1") && !AppSettings::check_admin_password(a2, "관리자pw2"), "관리자 비밀번호: 해시로만 저장, 맞을 때만 통과");
		AppSettings::save(a2);
		const AppConfig a3 = AppSettings::load();
		CHECK(AppSettings::check_admin_password(a3, "관리자pw1"), "관리자 비밀번호 저장·불러오기");
		AppSettings::save(r);
	}

	// ---- 백업·복원 ----
	{
		const QDir bd(AppSettings::backup_dir());
		const int autos0 = int(bd.entryList(QStringList() << "auto-*.ini", QDir::Files).size());
		CHECK(autos0 >= 1, "저장할 때 자동 백업 생성 (auto-*.ini)");
		AppSettings::save(r);	// 내용 같음 → 새 자동 백업 없음
		CHECK(bd.entryList(QStringList() << "auto-*.ini", QDir::Files).size() == autos0, "내용이 같으면 자동 백업을 더 만들지 않음");
		const QString manual = AppSettings::backup_now();
		int n = -1;
		QString err;
		CHECK(!manual.isEmpty() && AppSettings::check_backup(manual, &n, &err) && n == 2, "수동 백업 + 백업 검사 (사용자 2명)");
		AppConfig one = r;
		one.users.removeLast();
		AppSettings::save(one);
		CHECK(AppSettings::load().users.size() == 1, "설정 변경 (사용자 1명)");
		const bool rok = AppSettings::restore_from(manual, &err);
		CHECK(rok, "백업으로 복원");
		const AppConfig back = AppSettings::load();
		CHECK(back.users.size() == 2 && back.users[1].master.password == "pw2", "복원 후 사용자 2명, 비밀번호까지 그대로 (같은 PC·계정)");
		CHECK(!bd.entryList(QStringList() << "before-restore-*.ini", QDir::Files).isEmpty(), "복원 직전 설정 자동 보관 (before-restore-*)");
		QFile junk(QDir(tmp.path()).filePath("junk.ini"));
		if (junk.open(QIODevice::WriteOnly)) { junk.write("hello=1\n"); junk.close(); }
		CHECK(!AppSettings::check_backup(junk.fileName(), nullptr, &err) && !AppSettings::restore_from(junk.fileName(), &err), "설정 파일이 아니면 복원 거부");
		for (int i = 0; i < 25; ++i) { AppConfig x = r; x.web_port = quint16(9000 + i); AppSettings::save(x); }
		CHECK(bd.entryList(QStringList() << "auto-*.ini", QDir::Files).size() <= 20, "자동 백업은 최근 20개만 보관");
	}

	std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	return g_fail ? 1 : 0;
}
