#include "rhi.h"

namespace toy3d
{
    void init_dynamic_rhi()
    {
        if (g_rhi == nullptr)
        {
            //g_rhi = new IDynamicRHI();
            //g_rhi->init();
        }
    }
    void clear_dynamic_rhi()
    {
        if (g_rhi != nullptr)
        {
            g_rhi->clear();
            delete g_rhi;
            g_rhi = nullptr;
        }
    }

}// namespace toy3d