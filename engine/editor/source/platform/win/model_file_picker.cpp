#include "platform/model_file_picker.h"

#include <filesystem>
#include <vector>

#include "platform/win/win32_window.h"
#include <commdlg.h>

namespace toy3d
{
    namespace
    {
    bool pick_files(IWindow& owner, std::vector<std::string>& paths, std::string& error, bool texture)
    {
        paths.clear();
        error.clear();
        auto* window = dynamic_cast<Win32Window*>(&owner);
        if (!window || !window->get_native_hwnd())
        { error = "Model file selection requires a native editor window."; return false; }
        std::vector<wchar_t> buffer(65536, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window->get_native_hwnd();
        dialog.lpstrTitle = texture ? L"Import Texture2D" : L"Import Static Mesh";
        dialog.lpstrFilter = texture ? L"Images (PNG, JPEG)\0*.png;*.jpg;*.jpeg\0All files\0*.*\0\0" :
            L"Static Mesh (FBX, OBJ, glTF, GLB)\0*.fbx;*.obj;*.gltf;*.glb\0All files\0*.*\0\0";
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT;
        if (!GetOpenFileNameW(&dialog))
        {
            const DWORD status = CommDlgExtendedError();
            if (status == 0) return true;
            error = "Model file selection failed (code " + std::to_string(status) + ").";
            return false;
        }
        try
        {
            // C++17 filesystem is used only for native UTF-16 path conversion
            // and multi-selection joining; shared Core handles all actual IO.
            const std::filesystem::path first(buffer.data());
            const wchar_t* next = buffer.data() + first.native().size() + 1;
            if (*next == L'\0') paths.push_back(first.u8string());
            else
            {
                while (*next != L'\0')
                {
                    const std::filesystem::path name(next);
                    if (paths.size() >= maximum_file_drop_paths)
                    { paths.clear(); error = "Select at most 32 model files."; return false; }
                    paths.push_back((first / name).u8string());
                    next += name.native().size() + 1;
                }
            }
        }
        catch (const std::exception& exception)
        { paths.clear(); error = std::string("Model path conversion failed: ") + exception.what(); return false; }
        return true;
    }
    }

    bool pick_model_files(IWindow& owner, std::vector<std::string>& paths, std::string& error)
    { return pick_files(owner, paths, error, false); }

    bool pick_texture_files(IWindow& owner, std::vector<std::string>& paths, std::string& error)
    { return pick_files(owner, paths, error, true); }
}
