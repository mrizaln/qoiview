#include <cassert>
#include <filesystem>

#include <qoipp.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <fmt/core.h>
#include <fmt/std.h>
#include <CLI/CLI.hpp>

namespace fs = std::filesystem;

constexpr auto vertexShader = R"glsl(
    #version 300 es

    layout(location = 0) in vec2 position;
    layout(location = 1) in vec2 texcoord;

    out vec2 v_texcoord;

    uniform vec2 offset;
    uniform vec2 aspect;
    uniform float zoom;

    void main()
    {
        gl_Position = vec4((position - offset) * aspect * zoom , 0.0, 1.0);
        v_texcoord = texcoord;
    }
)glsl";

constexpr auto fragmentShader = R"glsl(
    #version 300 es

    precision mediump float;

    in vec2 v_texcoord;
    out vec4 fragColor;

    uniform sampler2D tex;

    void main()
    {
        fragColor = texture(tex, v_texcoord);
    }
)glsl";

constexpr auto vertices = std::array{
    -1.0f, 1.0f,  0.0f, 1.0f,    // top-left
    1.0f,  1.0f,  1.0f, 1.0f,    // top-right
    1.0f,  -1.0f, 1.0f, 0.0f,    // bottom-right
    -1.0f, -1.0f, 0.0f, 0.0f,    // bottom-left
};

constexpr auto indices = std::array{
    0, 1, 2,    // upper-right triangle
    2, 3, 0,    // lower-left triangle
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

class QoiView
{
public:
    QoiView(GLFWwindow* window, fs::path path)
        : m_window{ window }
        , m_path{ std::move(path) }
    {
        glfwSetWindowUserPointer(m_window, this);

        prepareRect();
        prepareShader();
        prepareTexture();
    }

    ~QoiView()
    {
        glDeleteTextures(1, &m_texture);
        glDeleteProgram(m_program);
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);
        glDeleteVertexArrays(1, &m_vao);
    }

    void run(int width, int height)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);

        glUseProgram(m_program);

        glBindVertexArray(m_vao);
        glClearColor(0.13f, 0.14f, 0.21f, 1.0f);

        glViewport(0, 0, width, height);
        updateAspect(width, height);

        glUniform1f(glGetUniformLocation(m_program, "zoom"), m_zoom);
        glUniform2f(glGetUniformLocation(m_program, "offset"), m_offset.m_x, m_offset.m_y);

        glfwSwapInterval(0);

        while (not glfwWindowShouldClose(m_window)) {
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, nullptr);

            glfwSwapBuffers(m_window);
            glfwWaitEvents();
        }
    }

    void updateAspect(int width, int height)
    {
        auto imageRatio  = static_cast<float>(m_imageSize.m_x) / static_cast<float>(m_imageSize.m_y);
        auto windowRatio = static_cast<float>(width) / static_cast<float>(height);

        if (imageRatio > windowRatio) {
            m_aspect.m_x = 1.0f;
            m_aspect.m_y = windowRatio / imageRatio;
        } else {
            m_aspect.m_x = imageRatio / windowRatio;
            m_aspect.m_y = 1.0f;
        }

        auto loc = glGetUniformLocation(m_program, "aspect");
        glUniform2f(loc, m_aspect.m_x, m_aspect.m_y);
    }

    void updateZoom(Zoom zoom)
    {
        if (zoom == Zoom::In) {
            m_zoom *= 1.1f;
        } else {
            m_zoom /= 1.1f;
        }

        auto loc = glGetUniformLocation(m_program, "zoom");
        glUniform1f(loc, m_zoom);
    }

    void updateOffset(Movement movement)
    {
        switch (movement) {
        case Movement::Up: m_offset.m_y += 0.1f / m_zoom; break;
        case Movement::Down: m_offset.m_y -= 0.1f / m_zoom; break;
        case Movement::Left: m_offset.m_x -= 0.1f / m_zoom; break;
        case Movement::Right: m_offset.m_x += 0.1f / m_zoom; break;
        }

        auto loc = glGetUniformLocation(m_program, "offset");
        glUniform2f(loc, m_offset.m_x, m_offset.m_y);
    }

    void incrementOffset(Vec2<> offset)
    {
        m_offset.m_x -= offset.m_x / m_aspect.m_x / m_zoom * 2.0f;
        m_offset.m_y -= offset.m_y / m_aspect.m_y / m_zoom * 2.0f;
        auto loc      = glGetUniformLocation(m_program, "offset");
        glUniform2f(loc, m_offset.m_x, m_offset.m_y);
    }

    void toggleFullscreen()
    {
        if (m_fullscreen) {
            glfwSetWindowMonitor(
                m_window, nullptr, m_windowPos.m_x, m_windowPos.m_y, m_windowSize.m_x, m_windowSize.m_y, 0
            );
        } else {
            glfwGetWindowPos(m_window, &m_windowPos.m_x, &m_windowPos.m_y);
            glfwGetWindowSize(m_window, &m_windowSize.m_x, &m_windowSize.m_y);

            auto* monitor = glfwGetPrimaryMonitor();
            auto* mode    = glfwGetVideoMode(monitor);

            glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }

        m_fullscreen = not m_fullscreen;
    }

    Vec2<> m_offset       = { 0.0f, 0.0f };
    Vec2<> m_aspect       = { 1.0f, 1.0f };
    Vec2<> m_mouse        = { 0.0f, 0.0f };
    float  m_zoom         = 1.0f;
    bool   m_mousePressed = false;
    bool   m_fullscreen   = false;

private:
    void prepareRect()
    {
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void prepareShader()
    {
        auto buf     = std::array<char, 1024>{};
        auto success = GLint{};

        auto vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vertexShader, nullptr);
        glCompileShader(vert);
        glGetShaderiv(vert, GL_COMPILE_STATUS, &success);

        if (success == GL_FALSE) {
            glGetShaderInfoLog(vert, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to compile vertex shader: {}", buf.data());
            std::exit(1);
        }

        auto frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fragmentShader, nullptr);
        glCompileShader(frag);
        glGetShaderiv(frag, GL_COMPILE_STATUS, &success);

        if (success == GL_FALSE) {
            glGetShaderInfoLog(frag, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to compile fragment shader: {}", buf.data());
            std::exit(1);
        }

        m_program = glCreateProgram();
        glAttachShader(m_program, vert);
        glAttachShader(m_program, frag);
        glLinkProgram(m_program);
        glGetProgramiv(m_program, GL_LINK_STATUS, &success);

        if (success == GL_FALSE) {
            glGetProgramInfoLog(m_program, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to link shader program: {}", buf.data());
            std::exit(1);
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void prepareTexture()
    {
        assert(fs::exists(m_path) and fs::is_regular_file(m_path));
        auto [data, desc] = qoipp::decodeFromFile(m_path, qoipp::Channels::RGBA, true);

        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        auto format = desc.m_channels == qoipp::Channels::RGB ? GL_RGB : GL_RGBA;
        auto width  = static_cast<GLint>(desc.m_width);
        auto height = static_cast<GLint>(desc.m_height);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data.data());
        glGenerateMipmap(GL_TEXTURE_2D);

        glUseProgram(m_program);
        auto loc = glGetUniformLocation(m_program, "tex");
        glUniform1i(loc, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture);

        m_imageSize = {
            .m_x = static_cast<int>(desc.m_width),
            .m_y = static_cast<int>(desc.m_height),
        };
    }

    GLFWwindow* m_window;
    fs::path    m_path;
    GLuint      m_vbo;
    GLuint      m_vao;
    GLuint      m_ebo;
    GLuint      m_program;
    GLuint      m_texture;

    Vec2<int> m_imageSize;
    Vec2<int> m_windowPos;
    Vec2<int> m_windowSize;
};

void errorCallback(int error, const char* description)
{
    fmt::println(stderr, "GLFW Error [{:#010x}]: {}", error, description);
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
    glViewport(0, 0, width, height);
    view.updateAspect(width, height);
}

void keyCallback(GLFWwindow* window, int key, int, int action, int)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
    if (action == GLFW_RELEASE) {
        return;
    }

    switch (key) {
    case GLFW_KEY_ESCAPE:
    case GLFW_KEY_Q: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_H: view.updateOffset(Movement::Left); break;
    case GLFW_KEY_L: view.updateOffset(Movement::Right); break;
    case GLFW_KEY_J: view.updateOffset(Movement::Down); break;
    case GLFW_KEY_K: view.updateOffset(Movement::Up); break;
    case GLFW_KEY_I: view.updateZoom(Zoom::In); break;
    case GLFW_KEY_O: view.updateZoom(Zoom::Out); break;
    case GLFW_KEY_F: view.toggleFullscreen(); break;
    }
}

void cursorCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    auto x = static_cast<float>(xpos);
    auto y = static_cast<float>(ypos);

    if (view.m_mousePressed) {
        auto dx = (x - view.m_mouse.m_x) / static_cast<float>(width);
        auto dy = (view.m_mouse.m_y - y) / static_cast<float>(height);
        view.incrementOffset({ dx, dy });
    }

    view.m_mouse = { x, y };
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        view.m_mousePressed = action == GLFW_PRESS;
    }
}

void scrollCallback(GLFWwindow* window, double, double yoffset)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
    if (yoffset > 0) {
        view.updateZoom(Zoom::In);
    } else {
        view.updateZoom(Zoom::Out);
    }
}

int main(int argc, char** argv)
{
    auto app = CLI::App{ "QoiView - A simple qoi image viewer" };

    auto input = fs::path{};
    app.add_option("input", input, "Input qoi image file")->required();

    if (argc == 1) {
        fmt::print(stderr, "{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    if (not fs::exists(input)) {
        fmt::println(stderr, "No such file '{}'", input.string());
        return 1;
    } else if (not fs::is_regular_file(input)) {
        fmt::println(stderr, "'{}' is not a regular file", input.string());
        return 1;
    }

    auto maybeHeader = qoipp::readHeaderFromFile(input);
    if (not maybeHeader) {
        fmt::println(stderr, "Failed to read qoi header from file '{}', possibly not a qoi image", input);
        return 1;
    }

    glfwSetErrorCallback(errorCallback);

    if (not glfwInit()) {
        fmt::println(stderr, "Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    auto* monitor = glfwGetPrimaryMonitor();
    auto* mode    = glfwGetVideoMode(monitor);

    auto monitorRatio = static_cast<float>(mode->width) / static_cast<float>(mode->height);
    auto imageRatio   = static_cast<float>(maybeHeader->m_width) / static_cast<float>(maybeHeader->m_height);

    auto width  = 0;
    auto height = 0;

    if (monitorRatio > imageRatio) {
        width  = static_cast<int>(static_cast<float>(mode->height) * imageRatio);
        height = mode->height;
    } else {
        width  = mode->width;
        height = static_cast<int>(static_cast<float>(mode->width) / imageRatio);
    }

    auto* window = glfwCreateWindow(width, height, "QoiView", nullptr, nullptr);
    if (window == nullptr) {
        fmt::println(stderr, "Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);

    auto view = QoiView{ window, input };
    view.run(width, height);
}
