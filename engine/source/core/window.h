#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

namespace toy3d
{
    class IWindow
    {
    public:
        IWindow(uint32_t width, uint32_t height);

        virtual ~IWindow();

        virtual bool should_close() = 0;

        virtual void process_events() = 0;

        virtual void close() = 0;

        virtual void resize(uint32_t width, uint32_t height);
    public:
        uint32_t get_width(){return m_width;};

        uint32_t get_height(){return m_height;};
    protected:
        uint32_t m_width;

        uint32_t m_height;

        std::string m_win_name;
    };
}// namespace toy3d