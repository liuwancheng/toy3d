#include "window.h"

namespace toy3d
{
    IWindow::IWindow(uint32_t width , uint32_t height)
    :m_width(width),
    m_height(height),
    m_win_name("Toy3d")
    {
    }

    IWindow::~IWindow(){}

    void IWindow::resize(uint32_t width, uint32_t height)
    {
        m_width = width;
        m_height = height;
        // todo: call platform function 
    }
}