// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// MainWindow — 간단한 제어판 (어르신도 쉽게): 서비스 켜기/끄기, 웹 접속 IP·포트, 자동 실행·자동 시작·와치독
//   사용자·백업·명단 등 나머지 설정은 웹(설정 페이지)에서. 창을 닫으면 트레이로 내려가 계속 동작
#pragma once
#include <QMainWindow>
#include <memory>
#include <vector>
#include "app_settings.h"
#include "session/call_log.h"
#include "session/id_db.h"
#include "web/sys_monitor.h"

class DashboardServer;
class IdDbUpdater;
class NetCheck;
class NoticeService;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTableWidget;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(bool start_hidden, bool recover, QWidget *parent = nullptr);
	~MainWindow() override;
	static QString clean_exit_flag();		// 정상 종료 표시 파일 (와치독이 보고 다시 실행할지 정함)
	// 묻지 않고 정상 종료. for_update = 설치 파일이 보낸 --quit → 서비스가 켜져 있었으면 다시 시작할 때 서비스도 다시 켬
	void quit_now(bool for_update = false);
	static QString resume_flag();			// 업데이트 뒤 서비스 다시 켜기 표시 파일

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	struct Row {
		std::unique_ptr<Session> session;
		QString master_state = "정지", client = "없음", air;
	};

	// ---- 서비스 ----
	bool set_service(bool on, QString *err = nullptr);
	void stop_all();
	bool running() const;
	void start_user(int i);
	QString apply_user(int i, const SessionConfig &c);	// i<0이면 추가. 반환 = 시작 전 확인할 문제
	void delete_user(int i);
	void watchdog_tick();
	void spawn_watchdog();
	void quit_app();

	// ---- 웹 ----
	QJsonObject web_config() const;
	QJsonObject web_action(const QString &action, const QJsonObject &body);
	void start_dashboard();
	// 브릿지 공지: targets = 핫스팟 ID 문자열 목록 (비우면 접속 중인 모든 사용자). 반환 = 보낸 사용자 수
	int notice_dispatch(const QVector<int16_t> &pcm, const QString &title, const QString &source, const QStringList &targets, bool ai = false);
	QJsonObject notice_action(const QString &action, const QJsonObject &body);
	QString local_url(const QString &path = QString()) const;
	QStringList access_urls(const QString &path) const;

	// ---- 화면 ----
	void build_ui();
	void build_tray();
	void refresh_status();
	void refresh_table();
	void refresh_row(int i);
	void append_log(const QString &s);
	void apply_network();
	void show_about();
	void check_vocoder(bool popup);
	QStringList vocoder_plugins() const;
	static bool autostart_enabled();
	static void set_autostart(bool on);

	// ---- ID 명단 ----
	void load_id_db();
	void download_id_db();
	QString id_db_path() const;

	AppConfig app_;
	std::vector<Row> rows_;			// app_.users와 같은 순서
	bool service_on_ = false;		// 사용자가 원하는 상태 (와치독이 이 상태를 유지)
	bool quitting_ = false;
	bool tray_notice_ = false;
	bool web_ok_ = false;
	QString id_status_;				// 명단 받기 진행·결과 (웹에 표시)
	SysMonitor sysmon_;

	QLabel *status_ = nullptr, *guide_ = nullptr, *urls_ = nullptr, *net_hint_ = nullptr;
	QPushButton *service_btn_ = nullptr;
	QComboBox *bind_ = nullptr;
	QSpinBox *port_ = nullptr;
	QCheckBox *autostart_ = nullptr, *auto_service_ = nullptr, *watchdog_ = nullptr;
	QTableWidget *table_ = nullptr;
	QPlainTextEdit *log_ = nullptr;
	QSystemTrayIcon *tray_ = nullptr;
	QAction *tray_service_ = nullptr;
	CallLog *calls_ = nullptr;
	DashboardServer *web_ = nullptr;
	IdDatabase ids_;
	IdDbUpdater *id_updater_ = nullptr;
	NetCheck *netcheck_ = nullptr;		// 내부망·외부망 점검 (웹 설정에서 실행)
	NoticeService *notice_ = nullptr;	// 공지 음성 만들기·예약
	QByteArray notice_wav_;				// 마지막으로 만든 공지 음성 (WAV) - 웹 미리 듣기
	int notice_wav_id_ = 0;
};
