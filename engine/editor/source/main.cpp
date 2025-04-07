#include <string>
#include <vector>
#include <codecvt>
#include <locale>

#ifdef WITH_WIN64
    #include <windows.h>
    #include <shellapi.h>
	#include <stringapiset.h>
#endif

#include "core/config/config_manager.h"
#include "core/config/command_line_parser.h"
#include "engine.h"

std::string wchar2string(const wchar_t* wstr) 
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

#if WITH_WIN64
	std::string WideToUtf8(const wchar_t* wide_str) 
	{
		if (!wide_str) return std::string();
		
		int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, nullptr, 0, nullptr, nullptr);
		if (requiredSize <= 0) return std::string();
		
		std::string result(requiredSize, 0);
		WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, &result[0], requiredSize, nullptr, nullptr);
		
		// 移除字符串末尾的null终止符
		if (!result.empty() && result.back() == 0)
			result.pop_back();
		
		return result;
	}
#endif



// 引擎主函数声明
int engine_main(void* hInstance);

#if WITH_WIN64
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
    toy3d::Engine engine;
	engine.init(hInstance);
	engine.main_loop();
	engine.exit();
    return 0;
}
