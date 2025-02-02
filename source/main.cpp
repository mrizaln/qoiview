#include <algorithm>
#include <cassert>
#include <filesystem>
#include <ranges>

#include <qoipp.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <fmt/core.h>
#include <CLI/CLI.hpp>

namespace fs = std::filesystem;
namespace sv = std::views;
namespace sr = std::ranges;

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
    QoiView(GLFWwindow* window, std::span<const fs::path> files, std::size_t start)
        : m_window{ window }
        , m_files{ files }
        , m_index{ start }
    {
        glfwSetWindowUserPointer(m_window, this);

        prepareRect();
        prepareShader();
        prepareTexture();

        m_monitor = glfwGetPrimaryMonitor();
        m_mode    = glfwGetVideoMode(m_monitor);
    }

    ~QoiView()
    {
        glDeleteTextures(1, &m_texture);
        glDeleteProgram(m_program);
        glDeleteBuffers(1, &m_ebo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
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

        applyUniform(Uniform::Zoom);
        applyUniform(Uniform::Offset);

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

        applyUniform(Uniform::Aspect);
        updateTitle();
    }

    void updateZoom(Zoom zoom)
    {
        if (zoom == Zoom::In) {
            m_zoom *= 1.1f;
        } else {
            m_zoom /= 1.1f;
        }

        applyUniform(Uniform::Zoom);
        updateTitle();
    }

    void updateOffset(Movement movement)
    {
        switch (movement) {
        case Movement::Up: m_offset.m_y += 0.1f / m_zoom; break;
        case Movement::Down: m_offset.m_y -= 0.1f / m_zoom; break;
        case Movement::Left: m_offset.m_x -= 0.1f / m_zoom; break;
        case Movement::Right: m_offset.m_x += 0.1f / m_zoom; break;
        }

        applyUniform(Uniform::Offset);
    }

    void incrementOffset(Vec2<> offset)
    {
        m_offset.m_x -= offset.m_x / m_aspect.m_x / m_zoom * 2.0f;
        m_offset.m_y -= offset.m_y / m_aspect.m_y / m_zoom * 2.0f;
        applyUniform(Uniform::Offset);
    }

    void toggleFullscreen()
    {
        if (glfwGetWindowMonitor(m_window) != nullptr) {
            auto [xpos, ypos]    = m_windowPos;
            auto [width, height] = m_windowSize;
            glfwSetWindowMonitor(m_window, nullptr, xpos, ypos, width, height, GLFW_DONT_CARE);
        } else {
            glfwGetWindowPos(m_window, &m_windowPos.m_x, &m_windowPos.m_y);
            glfwGetWindowSize(m_window, &m_windowSize.m_x, &m_windowSize.m_y);

            const auto& mode = *m_mode;
            glfwSetWindowMonitor(m_window, m_monitor, 0, 0, mode.width, mode.height, mode.refreshRate);
        }
    }

    void toggleFiltering()
    {
        auto count  = static_cast<int>(Filter::count);
        auto filter = static_cast<Filter>((static_cast<int>(m_filter) + 1) % count);
        updateFiltering(filter, m_mipmap);
        updateTitle();
    }

    void toggleMipmap()
    {
        updateFiltering(m_filter, not m_mipmap);
        updateTitle();
    }

    void nextFile()
    {
        if (m_files.size() == 1) {
            return;
        }

        m_index = (m_index + 1) % m_files.size();
        prepareTexture();

        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        updateAspect(width, height);
    }

    void previousFile()
    {
        if (m_files.size() == 1) {
            return;
        }

        m_index = (m_index - 1) % m_files.size();
        prepareTexture();

        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        updateAspect(width, height);
    }

    void resetZoom()
    {
        m_zoom = 1.0f;
        applyUniform(Uniform::Zoom);
        updateTitle();
    }

    void resetOffset()
    {
        m_offset = { 0.0f, 0.0f };
        applyUniform(Uniform::Offset);
    }

    void updateTitle()
    {
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);

        auto windowScale = static_cast<float>(width) / static_cast<float>(m_mode->width);
        auto imageScale  = static_cast<float>(m_imageSize.m_x) / static_cast<float>(m_mode->width);
        auto zoom        = static_cast<int>(m_zoom * 100.0f * windowScale / imageScale * m_aspect.m_x);

        auto title = fmt::format(
            "[{}/{}] [{}x{}] [{}%] QoiView - {} [filter:{}|mipmap:{}]",
            m_index + 1,
            m_files.size(),
            m_imageSize.m_x,
            m_imageSize.m_y,
            zoom,
            currentFile().filename().string(),
            m_filter == Filter::Linear ? "linear" : "nearest",
            m_mipmap ? "yes" : "no"
        );
        glfwSetWindowTitle(m_window, title.c_str());
    }

    const fs::path& currentFile() const
    {
        return m_files[m_index];    //
    }

    Vec2<> m_offset       = { 0.0f, 0.0f };
    Vec2<> m_aspect       = { 1.0f, 1.0f };
    Vec2<> m_mouse        = { 0.0f, 0.0f };
    float  m_zoom         = 1.0f;    // relative to window size
    Filter m_filter       = Filter::Linear;
    bool   m_mipmap       = true;
    bool   m_mousePressed = false;

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
            glfwTerminate();
            std::exit(1);
        }

        auto frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fragmentShader, nullptr);
        glCompileShader(frag);
        glGetShaderiv(frag, GL_COMPILE_STATUS, &success);

        if (success == GL_FALSE) {
            glGetShaderInfoLog(frag, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to compile fragment shader: {}", buf.data());
            glfwTerminate();
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
            glfwTerminate();
            std::exit(1);
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void prepareTexture()
    {
        const auto& file = currentFile();
        assert(fs::exists(file) and fs::is_regular_file(file));
        auto [data, desc] = qoipp::decodeFromFile(file, qoipp::Channels::RGBA, true);

        if (m_texture != 0) {
            glDeleteTextures(1, &m_texture);
        }

        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        auto format = desc.m_channels == qoipp::Channels::RGB ? GL_RGB : GL_RGBA;
        auto width  = static_cast<GLint>(desc.m_width);
        auto height = static_cast<GLint>(desc.m_height);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data.data());
        glGenerateMipmap(GL_TEXTURE_2D);

        glUseProgram(m_program);
        applyUniform(Uniform::Tex);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture);

        m_imageSize = {
            .m_x = static_cast<int>(desc.m_width),
            .m_y = static_cast<int>(desc.m_height),
        };
    }

    void updateFiltering(Filter filter, bool mipmap)
    {
        m_filter = filter;
        m_mipmap = mipmap;

        if (m_filter == Filter::Linear) {
            auto min = m_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            auto min = m_mipmap ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
    }

    void applyUniform(Uniform uniform)
    {
        auto loc = [this](const char* name) { return glGetUniformLocation(m_program, name); };
        switch (uniform) {
        case Uniform::Zoom: glUniform1f(loc("zoom"), m_zoom); break;
        case Uniform::Offset: glUniform2f(loc("offset"), m_offset.m_x, m_offset.m_y); break;
        case Uniform::Aspect: glUniform2f(loc("aspect"), m_aspect.m_x, m_aspect.m_y); break;
        case Uniform::Tex: glUniform1i(loc("tex"), 0); break;
        }
    }

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
    case GLFW_KEY_N: view.toggleFiltering(); break;
    case GLFW_KEY_M: view.toggleMipmap(); break;
    case GLFW_KEY_R: (view.resetZoom(), view.resetOffset()); break;
    case GLFW_KEY_P: fmt::println("{}", view.currentFile().c_str()); break;
    case GLFW_KEY_UP: view.updateZoom(Zoom::In); break;
    case GLFW_KEY_DOWN: view.updateZoom(Zoom::Out); break;
    case GLFW_KEY_RIGHT: view.nextFile(); break;
    case GLFW_KEY_LEFT: view.previousFile(); break;
    }
}

void cursorCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));

    auto x = static_cast<float>(xpos);
    auto y = static_cast<float>(ypos);

    if (view.m_mousePressed) {
        int width, height;
        glfwGetWindowSize(window, &width, &height);

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

std::optional<Inputs> getQoiFiles(std::span<const fs::path> inputs)
{
    auto result          = std::optional<Inputs>{ std::in_place, std::vector<fs::path>{}, 0 };
    auto& [files, start] = result.value();

    auto isQoi = [](const fs::path& path) {
        return fs::is_regular_file(path) and qoipp::readHeaderFromFile(path).has_value();
    };

    if (inputs.size() == 1) {
        auto input = inputs.front();

        if (not fs::exists(input)) {
            fmt::println(stderr, "No such file or directory '{}'", input.c_str());
            return {};
        } else if (fs::is_directory(input)) {
            for (auto file : fs::directory_iterator(input) | sv::filter(isQoi)) {
                files.push_back(file);
            }
            if (files.empty()) {
                fmt::println(stderr, "No valid qoi files found in '{}' directory", input.c_str());
                return {};
            }
        } else if (fs::is_regular_file(input)) {
            if (isQoi(input)) {
                auto parent = fs::directory_iterator{ fs::canonical(input).parent_path() };
                for (auto input : parent | sv::filter(isQoi)) {
                    files.push_back(input);
                }
                auto isInput = [&](const fs::path& path) { return fs::equivalent(path, input); };
                start        = static_cast<std::size_t>(sr::find_if(files, isInput) - files.begin());
            } else {
                fmt::println(stderr, "Not a valid qoi file '{}'", input.c_str());
                return {};
            }
        } else {
            fmt::println(stderr, "Not a regular file or directory '{}'", input.c_str());
            return {};
        }
    } else {
        for (auto input : inputs | sv::filter(isQoi)) {
            files.push_back(input);
        }
        if (files.empty()) {
            fmt::println(stderr, "No valid qoi files found in input arguments");
            return {};
        }
    }

    return result;
}

int main(int argc, char** argv)
{
    auto app = CLI::App{ "QoiView - A simple qoi image viewer" };

    auto files = std::vector<fs::path>{};
    app.add_option("files", files, "Input qoi file or directory")->required()->check(CLI::ExistingPath);

    if (argc == 1) {
        fmt::print(stderr, "{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    auto inputs = getQoiFiles(files);
    if (not inputs.has_value()) {
        return 1;
    }

    if (not glfwInit()) {
        fmt::println(stderr, "Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    auto* monitor = glfwGetPrimaryMonitor();
    auto* mode    = glfwGetVideoMode(monitor);

    auto header = qoipp::readHeaderFromFile(inputs->m_files[inputs->m_start]).value();

    auto width  = 0;
    auto height = 0;

    if (static_cast<int>(header.m_height) > mode->height or static_cast<int>(header.m_width) > mode->width) {
        auto monitorRatio = static_cast<float>(mode->width) / static_cast<float>(mode->height);
        auto imageRatio   = static_cast<float>(header.m_width) / static_cast<float>(header.m_height);

        if (monitorRatio > imageRatio) {
            width  = static_cast<int>(static_cast<float>(mode->height) * imageRatio);
            height = mode->height;
        } else {
            width  = mode->width;
            height = static_cast<int>(static_cast<float>(mode->width) / imageRatio);
        }
    } else {
        width  = static_cast<int>(header.m_width);
        height = static_cast<int>(header.m_height);
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

    auto view = QoiView{ window, inputs->m_files, inputs->m_start };
    view.run(width, height);

    glfwTerminate();
}
