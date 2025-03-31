#if WITH_ANDROID
#include <game-activity/native_app_glue/android_native_app_glue.h>
extern std::unique_ptr<vkb::PlatformContext> create_platform_context(android_app *state);
                 
		int  platform_main(const vkb::PlatformContext &);  
		void android_main(android_app *state)              
		{                                                  
			auto context = create_platform_context(state); 
			platform_main(*context);                       
		}                                                  
		int platform_main(const vkb::PlatformContext &context_name)
#elif WITH_WIN64
#include <Windows.h>
    extern std::unique_ptr<vkb::PlatformContext> create_platform_context(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow);
                                                              
    int          platform_main(const vkb::PlatformContext &);                                        
    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow) 
    {                                                                                                
        auto context = create_platform_context(hInstance, hPrevInstance, lpCmdLine, nCmdShow);       
        return platform_main(*context);                                                              
    }                                                                                                
    int platform_main(const vkb::PlatformContext &context_name)
#elif WITH_MAC
extern std::unique_ptr<vkb::PlatformContext> create_platform_context(int argc, char **argv);
                      
		int platform_main(const vkb::PlatformContext &);        
		int main(int argc, char *argv[])                        
		{                                                       
			auto context = create_platform_context(argc, argv); 
			return platform_main(*context);                     
		}                                                       
		int platform_main(const vkb::PlatformContext &context_name)

#else
	include <stdexcept>                      
		int main(int argc, char *argv[])                        
		{                                                       
			throw std::runtime_error{"platform not supported"}; 
		}                                                       
		int unused(const vkb::PlatformContext &context_name)
#endif