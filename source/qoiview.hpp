#pragma once

#include "common.hpp"

#include <cassert>
#include <qoipp.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <glad/glad.h>

#include <vector>

namespace qoiview
{
    struct Inputs
    {
        std::vector<fs::path> m_files;
        std::size_t           m_start;
    };

    template <typename T = float>
    struct Vec2
    {
        T m_x;
        T m_y;
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

        const fs::path& currentFile() const { return m_files[m_index]; }

    private:
        static void errorCallback(int error, const char* description);
        static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
        static void keyCallback(GLFWwindow* window, int key, int, int action, int);
        static void cursorCallback(GLFWwindow* window, double xpos, double ypos);
        static void mouseButtonCallback(GLFWwindow* window, int button, int action, int);
        static void scrollCallback(GLFWwindow* window, double, double yoffset);

        void updateAspect(int width, int height);
        void updateZoom(Zoom zoom);
        void updateOffset(Movement movement);
        void incrementOffset(Vec2<> offset);
        void toggleFullscreen();
        void toggleFiltering();
        void toggleMipmap();
        void nextFile();
        void previousFile();
        void resetZoom();
        void resetOffset();
        void updateTitle();
        void prepareRect();
        void prepareShader();
        void prepareTexture();
        void updateFiltering(Filter filter, bool mipmap);
        void applyUniform(Uniform uniform);

        Vec2<> m_offset       = { 0.0f, 0.0f };
        Vec2<> m_aspect       = { 1.0f, 1.0f };
        Vec2<> m_mouse        = { 0.0f, 0.0f };
        float  m_zoom         = 1.0f;    // relative to window size
        Filter m_filter       = Filter::Linear;
        bool   m_mipmap       = true;
        bool   m_mousePressed = false;

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

        Vec2<int> m_imageSize;
        Vec2<int> m_windowPos;
        Vec2<int> m_windowSize;
    };

    std::optional<Inputs> getQoiFiles(std::span<const fs::path> inputs);
}
