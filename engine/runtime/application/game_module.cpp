#include "application/game_module.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include "asset/asset_identity.h"
#include "file_system/native_platform_file.h"
#include "platform/platform_services.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // GameModuleLibrary: validate the native module before publishing callbacks
    // --------------------------------------------------------------------------
    GameModuleLibrary::~GameModuleLibrary()
    {
        library_.close();
        if (!loaded_copy_.empty())
        {
            NativePlatformFile platform;
            const auto removed = platform.remove_file(loaded_copy_);
            if (!removed.succeeded() && removed.code != FileErrorCode::NotFound)
            {
                std::cerr << "Project module copy cleanup: " << removed.message << '\n';
            }
            const auto parent = platform.parent_path(loaded_copy_);
            if (parent.succeeded())
            {
                const auto removed_directory = platform.remove_empty_directory(parent.value());
                if (!removed_directory.succeeded() && removed_directory.code != FileErrorCode::NotFound)
                {
                    std::cerr << "Project module directory cleanup: " << removed_directory.message << '\n';
                }
            }
        }
    }
    bool GameModuleLibrary::load(const PhysicalPath& path, const std::string& expected_name, std::string& error)
    {
        if (!loaded_copy_.empty())
        {
            error = "A project module owner cannot be reused.";
            return false;
        }
        NativePlatformFile platform;
        try
        {
            // filesystem rejects cwd-dependent input before creating the owned load copy.
            if (!path.valid() || !std::filesystem::u8path(path.utf8()).is_absolute())
            {
                error = "Project module requires an absolute path.";
                return false;
            }
        }
        catch (const std::filesystem::filesystem_error& exception)
        {
            error = exception.what();
            return false;
        }
        const auto bytes = platform.read_binary(path);
        const auto user = user_data_directory();
        AssetId session;
        if (!bytes.succeeded() || !user.succeeded() || !AssetId::try_generate(session))
        {
            error = "Cannot read the project module or prepare its owned load copy: " + path.utf8();
            return false;
        }
        const auto directory = PhysicalPath(user.value().utf8() + "/Toy3d/modules/" + session.hex());
        const auto made = platform.create_directories(directory);
        loaded_copy_ = PhysicalPath(directory.utf8() + "/" + path.utf8().substr(path.utf8().find_last_of("/\\") + 1u));
        const auto written =
            made.succeeded() ? platform.write_binary(loaded_copy_, bytes.value(), FileWriteMode::CreateNew) : made;
        if (!written.succeeded())
        {
            error = written.message;
            return false;
        }
        // Load an owned copy so a build can publish a new module while this
        // process keeps its original callbacks. Changes still require restart.
        if (!library_.open(loaded_copy_, error))
        {
            return false;
        }
        const auto entry = reinterpret_cast<const GameModuleApi* (*)()>(library_.symbol("toy3d_game_module", error));
        if (!entry)
        {
            library_.close();
            return false;
        }
        const GameModuleApi* api = nullptr;
        try
        {
            api = entry();
        }
        catch (const std::exception& exception)
        {
            error = "Project module entry failed: " + std::string(exception.what());
            library_.close();
            return false;
        }
        if (!api || api->abi_version != 1u || !api->build_identity ||
            std::strcmp(api->build_identity, TOY3D_MODULE_BUILD_ID) != 0 || !api->name || expected_name != api->name ||
            !api->register_types)
        {
            error = "Project module ABI/build identity/name is incompatible: " + path.utf8();
            library_.close();
            return false;
        }
        registration_ = {api->name, api->register_types};
        return true;
    }
} // namespace toy3d
