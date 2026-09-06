#pragma once

#include "task_graph/task_graph_types.h"

namespace toy3d
{
    class TaskGraphInterface;

    namespace render_command_detail
    {
        // RenderingThread owns this lifecycle seam. It is not part of the public
        // RenderCommand producer façade.
        TaskGraphStatus enable_render_command_facade(TaskGraphInterface& task_graph) noexcept;
        void disable_render_command_execution(TaskGraphInterface& task_graph) noexcept;
        void disable_render_command_facade(TaskGraphInterface& task_graph) noexcept;
    } // namespace render_command_detail
} // namespace toy3d
