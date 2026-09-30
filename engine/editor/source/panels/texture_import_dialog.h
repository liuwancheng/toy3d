#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "asset_identity.h"
#include "task_graph/graph_event.h"

namespace toy3d
{
    class IWindow;
    class EditorWorkspace;
    class EditorSelection;

    class TextureImportDialog final
    {
      public:
        bool request(const std::string& folder, const std::vector<std::string>& sources = {});
        void draw(IWindow& window, EditorWorkspace& workspace, EditorSelection& selection);
        bool active() const { return active_; }
        const std::string& error() const { return error_; }

      private:
        struct Candidate
        {
            std::array<char, 4097> source{};
            std::array<char, 256> name{};
            std::string error;
        };
        struct Prepared
        {
            AssetId id;
            std::string destination;
            std::vector<std::uint8_t> bytes;
            std::string error;
            bool saved = false;
        };
        void clear();
        void set_sources(const std::vector<std::string>& sources);
        std::vector<Candidate> candidates_;
        std::vector<Candidate> failed_;
        std::shared_ptr<Prepared> prepared_;
        GraphEventRef task_;
        std::size_t next_ = 0;
        std::string folder_;
        std::string error_;
        bool active_ = false;
        bool open_ = false;
        bool browse_ = false;
        bool running_ = false;
    };
} // namespace toy3d
