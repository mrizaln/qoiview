#pragma once

#include "common.hpp"

#include <qoipp/simple.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <glad/glad.h>

#include <cassert>
#include <vector>

namespace qoiview
{
    struct Inputs
    {
        std::vector<fs::path> files;
        std::size_t           start;
    };

    template <typename T = float>
    struct Vec2
    {
        T x;
        T y;
    };

    enum class Movement
    {
        Up,
        Down,
        Left,
        Right,
    };

    enum class Zoom
    {
        In,
        Out,
    };

    enum class Filter
    {
        Linear,
        Nearest,

        count
    };

    enum class Uniform
    {
        Zoom,
        Offset,
        Aspect,
        Tex,
    };

    class QoiView
    {
    public:
        QoiView(GLFWwindow* window, std::span<const fs::path> files, std::size_t start);
        ~QoiView();

        void run(int width, int height);

        const fs::path& current_file() const { return m_files[m_index]; }

    private:
        static void callback_error(int error, const char* description);
        static void callback_framebuffer_size(GLFWwindow* window, int width, int height);
        static void callback_key(GLFWwindow* window, int key, int, int action, int);
        static void callback_cursor(GLFWwindow* window, double xpos, double ypos);
        static void callback_mouse_button(GLFWwindow* window, int button, int action, int);
        static void callback_scroll(GLFWwindow* window, double, double yoffset);

        void update_aspect(int width, int height);
        void update_zoom(Zoom zoom);
        void update_offset(Movement movement);
        void increment_offset(Vec2<> offset);
        void toggle_fullscreen();
        void toggle_filtering();
        void toggle_mipmap();
        void file_next();
        void file_previous();
        void reset_zoom();
        void reset_offset();
        void update_title();
        void prepare_rect();
        void prepare_shader();
        void prepare_texture();
        void update_filtering(Filter filter, bool mipmap);
        void apply_uniform(Uniform uniform);

        Vec2<> m_offset      = { 0.0f, 0.0f };
        Vec2<> m_aspect      = { 1.0f, 1.0f };
        Vec2<> m_mouse       = { 0.0f, 0.0f };
        float  m_zoom        = 1.0f;    // relative to window size
        Filter m_filter      = Filter::Linear;
        bool   m_mipmap      = true;
        bool   m_mouse_press = false;

        GLFWwindow*        m_window;
        GLFWmonitor*       m_monitor;
        const GLFWvidmode* m_mode;

        GLuint m_vbo;
        GLuint m_vao;
        GLuint m_ebo;
        GLuint m_program;
        GLuint m_texture;

        std::span<const fs::path> m_files;
        std::size_t               m_index;

        Vec2<int> m_image_size;
        Vec2<int> m_window_pos;
        Vec2<int> m_window_size;
    };

    std::optional<Inputs> get_qoi_files(std::span<const fs::path> inputs);
}
