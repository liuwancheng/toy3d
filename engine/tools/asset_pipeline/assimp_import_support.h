#pragma once

#include "asset_pipeline/static_mesh_import.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/scene.h>

#include "math/matrix4.h"

namespace toy3d
{
    namespace assimp_import
    {
        // --------------------------------------------------------------------------
        // ImportStream: owns bounded source bytes and Assimp's read cursor
        // --------------------------------------------------------------------------
        class ImportStream final : public Assimp::IOStream
        {
          public:
            explicit ImportStream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes))
            {
            }
            std::size_t Read(void* buffer, std::size_t size, std::size_t count) override
            {
                if (size == 0 || !buffer)
                {
                    return 0;
                }
                const std::size_t items = std::min(count, (bytes_.size() - cursor_) / size);
                if (items != 0)
                {
                    std::memcpy(buffer, bytes_.data() + cursor_, items * size);
                }
                cursor_ += items * size;
                return items;
            }
            std::size_t Write(const void*, std::size_t, std::size_t) override
            {
                return 0;
            }
            aiReturn Seek(std::size_t offset, aiOrigin origin) override
            {
                std::size_t next = 0;
                if (origin == aiOrigin_SET)
                {
                    next = offset;
                }
                else if (origin == aiOrigin_CUR)
                {
                    if (offset > bytes_.size() - cursor_)
                    {
                        return aiReturn_FAILURE;
                    }
                    next = cursor_ + offset;
                }
                else if (origin == aiOrigin_END)
                {
                    if (offset > bytes_.size())
                    {
                        return aiReturn_FAILURE;
                    }
                    next = bytes_.size() - offset;
                }
                else
                {
                    return aiReturn_FAILURE;
                }
                if (next > bytes_.size())
                {
                    return aiReturn_FAILURE;
                }
                cursor_ = next;
                return aiReturn_SUCCESS;
            }
            std::size_t Tell() const override
            {
                return cursor_;
            }
            std::size_t FileSize() const override
            {
                return bytes_.size();
            }
            void Flush() override
            {
            }

          private:
            std::vector<std::uint8_t> bytes_;
            std::size_t cursor_ = 0;
        };

        // --------------------------------------------------------------------------
        // ImportIO: restricts every Assimp source read to the injected source mount
        // --------------------------------------------------------------------------
        class ImportIO final : public Assimp::IOSystem
        {
          public:
            ImportIO(const FileSystem& files, std::string root) : files_(files), root_(std::move(root))
            {
            }
            bool Exists(const char* file) const override
            {
                const auto path = path_for(file);
                if (!path.succeeded())
                {
                    return false;
                }
                const auto info = files_.stat(path.value());
                return info.succeeded() && info.value().type == FileType::File;
            }
            char getOsSeparator() const override
            {
                return '/';
            }
            Assimp::IOStream* Open(const char* file, const char* mode) override
            {
                if (!mode ||
                    (std::strcmp(mode, "rb") != 0 && std::strcmp(mode, "r") != 0 && std::strcmp(mode, "rt") != 0))
                {
                    return nullptr;
                }
                const auto path = path_for(file);
                if (!path.succeeded())
                {
                    failures.push_back("Assimp source path is outside the import directory or malformed.");
                    return nullptr;
                }
                constexpr std::size_t max_source_bytes = 64u * 1024u * 1024u;
                constexpr std::size_t max_total_source_bytes = 256u * 1024u * 1024u;
                constexpr std::size_t max_source_reads = 256;
                if (++source_reads_ > max_source_reads || source_bytes_ >= max_total_source_bytes)
                {
                    failures.push_back("Assimp source reads exceed the aggregate import budget.");
                    return nullptr;
                }
                const auto bytes = files_.read_binary(
                    path.value(), std::min(max_source_bytes, max_total_source_bytes - source_bytes_));
                if (!bytes.succeeded())
                {
                    failures.push_back(path.value().utf8() + ": " + bytes.status().message);
                    return nullptr;
                }
                source_bytes_ += bytes.value().size();
                // Assimp owns returned streams and closes them through Close().
                return std::make_unique<ImportStream>(bytes.value()).release();
            }
            void Close(Assimp::IOStream* stream) override
            {
                const std::unique_ptr<Assimp::IOStream> owned(stream);
            }
            bool ComparePaths(const char* one, const char* two) const override
            {
                const auto a = path_for(one);
                const auto b = path_for(two);
                return a.succeeded() && b.succeeded() && a.value().utf8() == b.value().utf8();
            }
            std::vector<std::string> failures;

          private:
            FileResult<VirtualPath> path_for(const char* file) const
            {
                std::string text = file ? file : "";
                std::replace(text.begin(), text.end(), '\\', '/');
                if (!text.empty() && text.front() != '/')
                {
                    text = root_ + "/" + text;
                }
                if (text.compare(0, root_.size() + 1, root_ + "/") != 0)
                {
                    text.clear();
                }
                return VirtualPath::parse(text);
            }
            const FileSystem& files_;
            std::string root_;
            std::size_t source_bytes_ = 0;
            std::size_t source_reads_ = 0;
        };

        inline Matrix4 matrix_from_assimp(const aiMatrix4x4& source)
        {
            return Matrix4(Vector4(source.a1, source.b1, source.c1, source.d1),
                           Vector4(source.a2, source.b2, source.c2, source.d2),
                           Vector4(source.a3, source.b3, source.c3, source.d3),
                           Vector4(source.a4, source.b4, source.c4, source.d4));
        }

        inline bool source_conversion(const aiScene& scene, bool fbx, const StaticMeshImportOptions& options,
                                      Matrix4& output)
        {
            float unit = options.convert_scene_unit ? options.source_unit_in_centimeters : 1.0f;
            if (fbx && options.convert_scene_unit && options.use_file_unit && scene.mMetaData &&
                scene.mMetaData->HasKey("UnitScaleFactor"))
            {
                // Assimp stores FBX UnitScaleFactor as float, in cm per source unit.
                // Ignore invalid file units when the caller explicitly bypasses them.
                if (!scene.mMetaData->Get("UnitScaleFactor", unit))
                {
                    return false;
                }
            }
            const float scale = unit * options.import_uniform_scale;
            if (!is_finite(scale) || scale <= 0)
            {
                return false;
            }
            output = Matrix4();
            if (!fbx)
            {
                output.at(0, 0) = scale;
                output.at(1, 1) = scale;
                output.at(2, 2) = -scale;
                return true;
            }
            // FBXConverter's root correction is disabled. Metadata drives this one
            // explicit axis/unit conversion; GlobalScale would apply units twice.
            std::int32_t right = 0, up = 1, front = 2;
            std::int32_t right_sign = 1, up_sign = 1, front_sign = -1;
            if (scene.mMetaData)
            {
                if (!scene.mMetaData->Get("CoordAxis", right) || !scene.mMetaData->Get("CoordAxisSign", right_sign) ||
                    !scene.mMetaData->Get("UpAxis", up) || !scene.mMetaData->Get("UpAxisSign", up_sign) ||
                    !scene.mMetaData->Get("FrontAxis", front) || !scene.mMetaData->Get("FrontAxisSign", front_sign))
                {
                    return false;
                }
            }
            if (right < 0 || right > 2 || up < 0 || up > 2 || front < 0 || front > 2 || right == up || right == front ||
                up == front || (right_sign != 1 && right_sign != -1) || (up_sign != 1 && up_sign != -1) ||
                (front_sign != 1 && front_sign != -1))
            {
                return false;
            }
            output = Matrix4(0);
            output.at(right, 0) = right_sign * scale;
            output.at(up, 1) = up_sign * scale;
            output.at(front, 2) = front_sign * scale;
            output.at(3, 3) = 1;
            return true;
        }

    } // namespace assimp_import
} // namespace toy3d
