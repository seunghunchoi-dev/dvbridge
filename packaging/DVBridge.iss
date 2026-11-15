; DVBridge 설치 파일 (Inno Setup 6). package.bat가 /DAppVer=… /DStage=… /DOutDir=… 로 컴파일한다.
; 업데이트: AppId가 같으므로 새 버전 설치 파일을 실행하면 같은 폴더에 덮어씀.
;   data(설정·이력·백업)와 plugins(보코더)는 건드리지 않음. 실행 중이면 닫도록 안내.
#ifndef AppVer
  #define AppVer "0.1"
#endif
#ifndef Stage
  #define Stage "C:\dev\DMR\dist\DVBridge-0.1-win64"
#endif
#ifndef OutDir
  #define OutDir "C:\dev\DMR\dist"
#endif

[Setup]
AppId={{60F6E84D-4A51-49A4-984B-6F3E4974D7BA}
AppName=DVBridge
AppVersion={#AppVer}
AppVerName=DVBridge {#AppVer}
AppPublisher=최승훈 (6K2KZR)
AppCopyright=Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR), MIT License
VersionInfoVersion={#AppVer}.0.0
VersionInfoDescription=DVBridge 설치
DefaultDirName={code:InstallDir}
DefaultGroupName=DVBridge
; 예전 이름(DMRBridge) 폴더에 설치돼 있으면 같은 곳의 DVBridge 폴더로 옮김 (설정·보코더·음성 팩도 함께). 그 밖에는 예전 위치 그대로
UsePreviousAppDir=no
DisableDirPage=auto
UsePreviousGroup=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#Stage}\LICENSE.txt
SetupIconFile=..\app\res\dvbridge.ico
UninstallDisplayIcon={app}\DVBridge.exe
UninstallDisplayName=DVBridge
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
OutputDir={#OutDir}
OutputBaseFilename=DVBridge-{#AppVer}-setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "desktopicon"; Description: "바탕 화면에 바로 가기 만들기"; GroupDescription: "추가 작업:"

[Dirs]
; 설정 폴더: 일반 사용자 권한으로도 쓸 수 있게 (프로그램 파일 자체는 보호된 채로 둠). 제거·업데이트해도 남김
Name: "{app}\data"; Permissions: users-modify; Flags: uninsneveruninstall
Name: "{app}\plugins"; Flags: uninsneveruninstall

[InstallDelete]
; 업데이트 때 이전 버전의 Qt 파일이 섞이지 않도록 프로그램 파일만 먼저 지움 (data·plugins는 그대로)
Type: files; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
; 예전 버전의 HTML 설명서·바로 가기 (설명서는 PDF 하나로)
Type: files; Name: "{app}\사용 설명서.html"
Type: files; Name: "{group}\사용 설명서 (인쇄용 PDF).lnk"
; 예전 이름(DMRBridge) 시절의 실행 파일·시작 메뉴·바탕 화면 바로 가기
Type: files; Name: "{app}\DMRBridge.exe"
Type: filesandordirs; Name: "{commonprograms}\DMRBridge"
Type: files; Name: "{commondesktop}\DMRBridge.lnk"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Excludes: "data\*,plugins\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\plugins\README.txt"; DestDir: "{app}\plugins"; Flags: ignoreversion

[UninstallDelete]
; 음성 팩(별도 설치 파일 DVBridge-voicepack-*-setup.exe로 넣은 voice 폴더)은 DVBridge를 제거할 때 함께 지움
Type: filesandordirs; Name: "{app}\voice"

[Icons]
Name: "{group}\DVBridge"; Filename: "{app}\DVBridge.exe"
Name: "{group}\설정 폴더 열기"; Filename: "{app}\data"
Name: "{group}\보코더 폴더 열기"; Filename: "{app}\plugins"
Name: "{group}\사용 설명서"; Filename: "{app}\사용 설명서.pdf"
Name: "{group}\추가 설치 안내"; Filename: "{app}\추가 설치 안내.txt"
Name: "{group}\DVBridge 제거"; Filename: "{uninstallexe}"
Name: "{autodesktop}\DVBridge"; Filename: "{app}\DVBridge.exe"; Tasks: desktopicon

[Run]
; 설치한 사용자 권한으로 실행 (비밀번호 암호화가 Windows 사용자별이라 관리자 권한으로 실행하면 안 됨)
Filename: "{app}\DVBridge.exe"; Description: "DVBridge 실행"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
// ---- 보코더 플러그인 (선택): 사용자가 가진 vocoder_*.dll 을 골라 plugins 에 함께 설치 ----
//   조용히 설치할 때는 /VOCODER="파일 경로" 로 지정 가능
var
  VocPage: TInputFileWizardPage;
  PrevDir: String;	// 설치 시작할 때의 예전 설치 위치 (설치 중에 레지스트리가 새 위치로 바뀌므로 미리 기억)

// ---- 설치 위치: 예전 이름(DMRBridge) 폴더 → DVBridge 폴더 ----
function OldInstallDir: String;
var
  S: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{60F6E84D-4A51-49A4-984B-6F3E4974D7BA}_is1', 'InstallLocation', S) and (S <> '') then
    Result := RemoveBackslashUnlessRoot(S);
end;

function InstallDir(Param: String): String;
var
  Old: String;
begin
  Old := OldInstallDir;
  if Old = '' then
    Result := ExpandConstant('{autopf}\DVBridge')
  else if CompareText(ExtractFileName(Old), 'DMRBridge') = 0 then
    Result := AddBackslash(ExtractFileDir(Old)) + 'DVBridge'
  else
    Result := Old;
end;

// 예전 폴더의 data(설정)·plugins(보코더)·voice(음성 팩)를 새 폴더로 복사하고 예전 폴더를 지움
procedure MoveOldDir;
var
  Old, New, Sub: String;
  I, RC: Integer;
  Ok: Boolean;
begin
  Old := PrevDir;
  New := ExpandConstant('{app}');
  // 예전 위치를 모르면(또는 이미 새 위치면) 바로 옆의 예전 이름 폴더를 찾아봄
  if ((Old = '') or (CompareText(Old, New) = 0)) and DirExists(AddBackslash(ExtractFileDir(New)) + 'DMRBridge\data') then
    Old := AddBackslash(ExtractFileDir(New)) + 'DMRBridge';
  if (Old = '') or (CompareText(Old, New) = 0) or not DirExists(Old) then Exit;
  Log('moving old install: ' + Old + ' -> ' + New);
  Ok := True;
  for I := 0 to 2 do
  begin
    case I of
      0: Sub := 'data';
      1: Sub := 'plugins';
    else
      Sub := 'voice';
    end;
    if DirExists(Old + '\' + Sub) then
    begin
      // 새 폴더에 없는 파일만 복사 (/XC /XN /XO)
      Exec(ExpandConstant('{sys}\robocopy.exe'), '"' + Old + '\' + Sub + '" "' + New + '\' + Sub + '" /E /XC /XN /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP',
           '', SW_HIDE, ewWaitUntilTerminated, RC);
      Log('move ' + Sub + ': robocopy ' + IntToStr(RC));
      if RC >= 8 then Ok := False;
    end;
  end;
  if Ok then
  begin
    if DelTree(Old, True, True, True) then Log('old folder removed: ' + Old)
    else Log('old folder not fully removed: ' + Old);
  end
  else
    Log('old folder kept (copy failed): ' + Old);
end;

function InitializeSetup: Boolean;
begin
  PrevDir := OldInstallDir;
  Result := True;
end;

procedure InitializeWizard;
begin
  VocPage := CreateInputFilePage(wpSelectTasks,
    '보코더 플러그인 (선택)',
    'DMR 음성을 쓰려면 보코더 플러그인이 필요합니다.',
    'DVBridge에는 보코더가 들어 있지 않습니다. 직접 구한 보코더 플러그인 파일(vocoder_*.dll)이 있으면 골라 주세요. 함께 설치합니다.' + #13#10 +
    '(보코더의 라이선스·특허 조건은 사용하시는 분이 직접 확인해 주세요.)' + #13#10 +
    '없으면 비워 두고 [다음]을 누르세요. 나중에 시작 메뉴 "보코더 폴더 열기"로 넣어도 됩니다.');
  VocPage.Add('보코더 플러그인 파일:', '보코더 플러그인 (vocoder_*.dll)|vocoder_*.dll|DLL 파일 (*.dll)|*.dll', '.dll');
  VocPage.Values[0] := ExpandConstant('{param:VOCODER|}');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  F: String;
begin
  Result := True;
  if CurPageID = VocPage.ID then
  begin
    F := Trim(VocPage.Values[0]);
    if F <> '' then
    begin
      if not FileExists(F) then
      begin
        MsgBox('파일을 찾을 수 없습니다:' + #13#10 + F, mbError, MB_OK);
        Result := False;
      end
      else if CompareText(ExtractFileExt(F), '.dll') <> 0 then
      begin
        MsgBox('보코더 플러그인은 .dll 파일입니다.', mbError, MB_OK);
        Result := False;
      end;
    end;
  end;
end;

procedure InstallVocoder;
var
  F, Name, Dir, Base: String;
  FR: TFindRec;
begin
  F := Trim(VocPage.Values[0]);
  if (F = '') or not FileExists(F) then Exit;
  Name := ExtractFileName(F);
  if Pos('vocoder_', Lowercase(Name)) <> 1 then Name := 'vocoder_' + Name;	// DVBridge가 찾는 이름 규칙
  Dir := ExpandConstant('{app}\plugins\');
  ForceDirectories(Dir);
  if not FileCopy(F, Dir + Name, False) then
  begin
    MsgBox('보코더 플러그인을 복사하지 못했습니다:' + #13#10 + F, mbError, MB_OK);
    Exit;
  end;
  Log('vocoder installed: ' + Dir + Name);
  // 같은 폴더의 그 플러그인 고지·라이선스 문서(이름이 같은 *.txt)도 함께
  Base := ExtractFilePath(F) + ChangeFileExt(ExtractFileName(F), '');
  if FindFirst(Base + '*.txt', FR) then
  try
    repeat
      FileCopy(ExtractFilePath(F) + FR.Name, Dir + FR.Name, False);
    until not FindNext(FR);
  finally
    FindClose(FR);
  end;
end;

// 예전 이름(DMRBridge)의 "Windows를 켜면 자동 실행" 항목을 새 실행 파일로 옮김
procedure MoveAutostart;
var
  Key: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Run';
  if RegValueExists(HKCU, Key, 'DMRBridge') then
  begin
    RegWriteStringValue(HKCU, Key, 'DVBridge', '"' + ExpandConstant('{app}\DVBridge.exe') + '" --tray');
    RegDeleteValue(HKCU, Key, 'DMRBridge');
    Log('autostart moved to DVBridge');
  end
  else if RegValueExists(HKCU, Key, 'DVBridge') then
  begin
    // 설치 폴더가 바뀌었으면 새 위치로
    RegWriteStringValue(HKCU, Key, 'DVBridge', '"' + ExpandConstant('{app}\DVBridge.exe') + '" --tray');
    Log('autostart path updated');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    MoveOldDir;
    InstallVocoder;
    MoveAutostart;
    // 예전 이름 시절 음성 팩의 도우미 이름을 새 이름으로 (같은 프로그램, 이름만 바뀜)
    if FileExists(ExpandConstant('{app}\voice\dmrbridge_tts.exe')) and not FileExists(ExpandConstant('{app}\voice\dvbridge_tts.exe')) then
      if RenameFile(ExpandConstant('{app}\voice\dmrbridge_tts.exe'), ExpandConstant('{app}\voice\dvbridge_tts.exe')) then Log('voice helper renamed');
  end;
end;

// 업데이트: 실행 중인 DVBridge에 정상 종료를 요청 (정상 종료라 와치독이 다시 띄우지 않음)
//   "DVBridgeRunning" 표시가 있는(= --quit를 아는) 버전이 실행 중일 때만. 예전 버전은 Windows 재시작 관리자가 닫음
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  Old: String;
begin
  if FileExists(ExpandConstant('{app}\DVBridge.exe')) and CheckForMutexes('DVBridgeRunning') then
  begin
    Exec(ExpandConstant('{app}\DVBridge.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
  // 예전 폴더(설치 위치가 바뀌는 경우)에서 실행 중인 DVBridge·예전 이름 DMRBridge에도 종료 요청
  Old := OldInstallDir;
  if (Old <> '') and FileExists(Old + '\DVBridge.exe') and CheckForMutexes('DVBridgeRunning') then
  begin
    Exec(Old + '\DVBridge.exe', '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
  if (Old <> '') and FileExists(Old + '\DMRBridge.exe') and CheckForMutexes('DMRBridgeRunning') then
  begin
    Exec(Old + '\DMRBridge.exe', '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and DirExists(ExpandConstant('{app}\data')) then
  begin
    if MsgBox('설정·이력·백업(data 폴더)도 지울까요?' + #13#10 + #13#10 +
              '다시 설치해서 계속 쓰려면 [아니요]를 누르세요.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{app}\data'), True, True, True);
  end;
end;
