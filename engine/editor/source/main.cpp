#include <string>
#include <vector>
#include <codecvt>
#include <locale>

#if WITH_WIN64
#include <windows.h>
#endif

#include "core/config/config_manager.h"
#include "core/config/command_line_parser.h"
#include "engine.h"

toy3d::Engine g_engine;

std::string wchar2string(const wchar_t* wstr) 
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

// 引擎主函数声明
int engine_main(void* hInstance);

#if WITH_WIN64

	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		if (ctrlType == CTRL_CLOSE_EVENT)
		{
			g_engine.exit();
			Sleep(500);
			return TRUE;
		}
		return FALSE;
	}

	int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) 
	{
		// attempt to attach to the parent process console if it exists
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
		{
			// No parent console, allocate a new one for this process
			if (!AllocConsole())
			{
				throw std::runtime_error{"AllocConsole error"};
			}
		}

		FILE *fp;
		freopen_s(&fp, "conin$", "r", stdin);
		freopen_s(&fp, "conout$", "w", stdout);
		freopen_s(&fp, "conout$", "w", stderr);

		// 注册处理函数
		SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

		// process command line args
		int argc = 0;
		LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
		std::vector<std::string> args;
		for (int i = 0; i < argc; i++) 
		{
			args.push_back(wchar2string(argvw[i]));
		}
		LocalFree(argvw);
		toy3d::CommandLineParser::get_instance().parser_args(args);

		// start engine
		return engine_main(static_cast<void*>(hInstance));
	}
#else
	int main(int argc, char* argv[]) 
	{
		// process command line args
		std::vector<std::string> args;
		for (int i = 0; i < argc; i++) 
		{
			args.push_back(argv[i]);
		}
		toy3d::CommandLineParser::get_instance().parser_args(args);

		// start engine
		return engine_main(nullptr);
	}
#endif

int engine_main(void* hInstance)
{
	g_engine.init(hInstance);
	g_engine.main_loop();
	g_engine.exit();
    return 0;
}
