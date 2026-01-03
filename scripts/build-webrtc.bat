@ECHO OFF
SET EL=0

ECHO Add depot_tools to PATH
set PATH=%DEPOT_TOOLS%;%PATH%
IF %ERRORLEVEL% NEQ 0 GOTO ERROR

ECHO Patch WebRTC sources for Windows toolchains
CALL python3 -c "import pathlib, re; p=pathlib.Path(r'..\..\download\src\rtc_base\task_utils\repeating_task.h'); s=p.read_text(encoding='utf-8'); pat=r'typename\s+std::result_of<decltype\s*\(&Closure::operator\(\)\)\(\s*Closure\s*\)>::type'; rep=r'decltype(std::declval<Closure>()())'; s2=re.sub(pat, rep, s); (p.write_text(s2, encoding='utf-8') if s2!=s else None)"
IF %ERRORLEVEL% NEQ 0 GOTO ERROR

ECHO ninja
call autoninja webrtc libjingle_peerconnection
IF %ERRORLEVEL% NEQ 0 GOTO ERROR

GOTO DONE

:ERROR
ECHO ERRORLEVEL^: %ERRORLEVEL%
SET EL=%ERRORLEVEL%

:DONE

EXIT /b %EL%
