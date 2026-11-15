; DVBridge 음성 팩 설치 파일 (Inno Setup 6) — 별도로 내려받아 설치하는 선택 추가 기능
;   설치된 DVBridge 폴더를 찾아 그 안의 voice\ 에 자연스러운 목소리(Supertonic 3)를 넣음
;   AI 음성 모델 라이선스(Open RAIL-M)에 동의해야 설치됨. 따로 제거하지 않고 DVBridge를 제거할 때 함께 지워짐
;   package.bat가 /DAppVer=… /DStage=…(DVBridge-voicepack-<ver> 폴더) /DOutDir=… 로 컴파일
#ifndef AppVer
  #define AppVer "0.1"
#endif
#ifndef Stage
  #define Stage "C:\dev\DMR\dist\DVBridge-voicepack-0.1"
#endif
#ifndef OutDir
  #define OutDir "C:\dev\DMR\dist"
#endif
#define MainAppKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\{60F6E84D-4A51-49A4-984B-6F3E4974D7BA}_is1"

[Setup]
AppId={{B7C3E6A2-5D41-4F2B-9E0A-3C8D7F14A6E9}
AppName=DVBridge 음성 팩
AppVersion={#AppVer}
AppVerName=DVBridge 음성 팩 {#AppVer} (자연스러운 목소리)
AppPublisher=최승훈 (6K2KZR)
AppCopyright=Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR), MIT License (모델은 Supertone Inc., Open RAIL-M)
VersionInfoVersion={#AppVer}.0.0
VersionInfoDescription=DVBridge 음성 팩 설치
DefaultDirName={code:MainAppDir}
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
Uninstallable=no
CreateUninstallRegKey=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#Stage}\MODEL_LICENSE.txt
InfoBeforeFile={#Stage}\voice\README.txt
SetupIconFile=..\app\res\dvbridge.ico
WizardStyle=modern
OutputDir={#OutDir}
OutputBaseFilename=DVBridge-voicepack-{#AppVer}-setup
Compression=lzma2/max
SolidCompression=yes
DiskSpanning=no

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Messages]
korean.SelectDirLabel3=DVBridge가 설치된 폴더를 확인하세요. 음성 팩은 그 안의 voice 폴더에 들어갑니다.
korean.LicenseLabel3=음성 팩의 AI 음성 모델(Supertonic 3, Supertone Inc.)은 아래 Open RAIL-M 라이선스와 사용 제한(부속서 A)을 따릅니다. 동의해야 설치할 수 있습니다.

[InstallDelete]
; 예전 음성 팩 파일을 지우고 새로 넣음
Type: filesandordirs; Name: "{app}\voice"

[Files]
Source: "{#Stage}\voice\*"; DestDir: "{app}\voice"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; 설치 전에 DVBridge가 켜져 있었으면 다시 켬 (음성 팩은 시작할 때 읽음). 설치한 사용자 권한으로
Filename: "{app}\DVBridge.exe"; Flags: nowait runasoriginaluser; Check: WasRunning

[Code]
var
  Running: Boolean;

function MainAppDir(Param: String): String;
var
  S: String;
begin
  if RegQueryStringValue(HKLM64, '{#MainAppKey}', 'InstallLocation', S) and (S <> '') then
    Result := RemoveBackslashUnlessRoot(S)
  else
    Result := ExpandConstant('{autopf}\DVBridge');
end;

function WasRunning: Boolean;
begin
  Result := Running;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
    if not FileExists(AddBackslash(WizardDirValue) + 'DVBridge.exe') then
    begin
      MsgBox('이 폴더에 DVBridge가 없습니다:' + #13#10 + WizardDirValue + #13#10 + #13#10 +
             'DVBridge를 먼저 설치하거나, DVBridge.exe가 있는 폴더를 골라 주세요.', mbError, MB_OK);
      Result := False;
    end;
end;

// 조용히 설치(/VERYSILENT)할 때도 DVBridge 폴더가 맞는지 확인
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  if not FileExists(ExpandConstant('{app}\DVBridge.exe')) then
  begin
    Result := 'DVBridge가 설치된 폴더를 찾지 못했습니다. DVBridge를 먼저 설치해 주세요.';
    Exit;
  end;
  Running := CheckForMutexes('DVBridgeRunning');
  if Running then
  begin
    Exec(ExpandConstant('{app}\DVBridge.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
end;
