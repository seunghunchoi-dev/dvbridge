// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// 설정 저장/불러오기: <실행 파일 폴더>\data\dvbridge.ini (쓸 수 없으면 %APPDATA%\DVBridge)
//   data/backup: 저장할 때마다 자동 백업(auto-*, 최근 20개), 수동 백업(backup-*), 복원 직전 백업(before-restore-*)
//   [users] 배열: 사용자별 세션 설정 (다중 사용자), 비밀번호는 Windows DPAPI(현재 Windows 사용자 전용)로 암호화
//   [web], [vocoder]: 공통 설정
//   예전 단일 사용자 형식([master]/[user]/[usrp]/[safety])은 불러올 때 사용자 1로 옮기고, 저장할 때 지움
#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include "session/session.h"

struct AppConfig {
	QList<SessionConfig> users;
	quint16 web_port = 8080;
	QString web_bind = "0.0.0.0";	// 웹 접속 허용 주소: 0.0.0.0 = 모든 네트워크, 127.0.0.1 = 이 PC만, 그 밖 = 그 IP만
	bool auto_service = true;		// 프로그램이 켜지면 서비스(전체 접속) 자동 시작
	bool watchdog = true;			// 문제 생기면 자동 복구 (꺼진 접속 다시 켜기 + 비정상 종료 시 다시 실행)
	bool service_on = false;		// 마지막 서비스 상태 (비정상 종료 후 복구용)
	QString plugin_dir;			// 비우면 실행파일 옆 plugins
	QString admin_salt, admin_hash;
	int intercom_tg = 99;			// 내부 채널 번호 (0 = 끔) - BM으로 나가지 않는 이 브릿지 안의 채널
	bool rx_target_only = true;		// 지금 채널만 듣기 (다른 그룹 TG 수신은 휴대폰으로 안 보냄)
	int echo_tg = 9999;				// 내부 에코 번호 (0 = 끔) - 말하면 DVBridge가 바로 되돌려 들려줌 (BM 안 거침)
	QJsonObject notice;				// 브릿지 공지 설정 (목소리·빠르기·알림음·예약 목록) - notice_service.h	// 웹 설정 관리자 비밀번호 (PBKDF2-SHA256, Base64). 비면 미설정
	QString effective_plugin_dir() const;
};

namespace AppSettings {

QString config_dir();					// 설정·이력·명단·로그 폴더
QString config_path();					// ini 파일 전체 경로
QString config_note();					// 저장 위치 안내 (예전 설정을 옮겨 왔거나 대체 위치를 쓸 때), 없으면 빈 문자열
QString backup_dir();					// config_dir()/backup
QString backup_now(const QString &dest = QString());	// 현재 설정 복사 (dest 비우면 backup 폴더에 날짜 이름), 실패 시 빈 문자열
bool check_backup(const QString &file, int *users, QString *err);	// 백업 파일 검사 (사용자 수)
bool restore_from(const QString &file, QString *err);	// 현재 설정을 백업해 두고 file로 바꿈
AppConfig load();
void save(const AppConfig &c);
void save_service_state(bool on);		// 마지막 서비스 상태 (data\state.ini)
SessionConfig default_user();			// 새 사용자 기본값 (BM 4501, 9990 개인, USRP 32001)
int hotspot_suffix(const SessionConfig &c);	// 핫스팟 ID 접미사 (0 = 7자리 ID 그대로)
// 켜진 사용자끼리 USRP 포트·핫스팟 ID가 겹치는지 등 → 문제 목록 (비면 정상)
QStringList validate(const AppConfig &c);

// 웹 설정용 변환: 비밀번호는 내보내지 않음(password_set만). 들어온 값 중 password가 비어 있으면 기존 비밀번호 유지
QJsonObject user_to_json(const SessionConfig &c);
bool user_from_json(const QJsonObject &o, SessionConfig &c, QString *err);

// 웹 설정 관리자 비밀번호 (해시만 저장)
bool has_admin_password(const AppConfig &a);
void set_admin_password(AppConfig &a, const QString &pw);
bool check_admin_password(const AppConfig &a, const QString &pw);

// DPAPI 암호화/복호화 (Base64 문자열). 실패 시 빈 문자열
QString protect(const QString &plain);
QString unprotect(const QString &b64);

} // namespace AppSettings
