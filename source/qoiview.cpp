#include "qoiview/qoiview.hpp"

#include <glbinding/gl/gl.h>

namespace
{
    constexpr auto vertex_shader = R"glsl(
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

    constexpr auto fragment_shader = R"glsl(
        #version 300 es

        precision mediump float;

        in vec2 v_texcoord;
        out vec4 fragcolor;

        uniform sampler2D tex;

        void main()
        {
            fragcolor = texture(tex, v_texcoord);
        }
    )glsl";

    // I flipped the y tex coords :P
    constexpr auto vertices = std::array{
        // -1.0f, 1.0f,  0.0f, 1.0f,    // top-left
        // 1.0f,  1.0f,  1.0f, 1.0f,    // top-right
        // 1.0f,  -1.0f, 1.0f, 0.0f,    // bottom-right
        // -1.0f, -1.0f, 0.0f, 0.0f,    // bottom-left
        -1.0f, 1.0f,  0.0f, 0.0f,    // top-left
        1.0f,  1.0f,  1.0f, 0.0f,    // top-right
        1.0f,  -1.0f, 1.0f, 1.0f,    // bottom-right
        -1.0f, -1.0f, 0.0f, 1.0f,    // bottom-left
    };

    constexpr auto indices = std::array{
        0, 1, 2,    // upper-right triangle
        2, 3, 0,    // lower-left triangle
    };
}

namespace qoiview
{
    QoiView::QoiView(GLFWwindow* window, std::deque<fs::path> files, std::size_t start)
        : m_window{ window }
        , m_files{ std::move(files) }
        , m_index{ start }
    {
        m_decoder.launch();

        glfwSetWindowUserPointer(m_window, this);

        glfwSetFramebufferSizeCallback(window, callback_framebuffer_size);
        glfwSetKeyCallback(window, callback_key);
        glfwSetCursorPosCallback(window, callback_cursor);
        glfwSetMouseButtonCallback(window, callback_mouse_button);
        glfwSetScrollCallback(window, callback_scroll);

        prepare_rect();
        prepare_shader();

        while (not prepare_texture() and m_files.empty()) {
            file_next();
        }

        if (m_files.empty()) {
            std::exit(1);
        }

        m_monitor = glfwGetPrimaryMonitor();
        m_mode    = glfwGetVideoMode(m_monitor);
    }

    QoiView::~QoiView()
    {
        gl::glDeleteTextures(1, &m_texture);
        gl::glDeleteProgram(m_program);
        gl::glDeleteBuffers(1, &m_ebo);
        gl::glDeleteVertexArrays(1, &m_vao);
        gl::glDeleteBuffers(1, &m_vbo);
    }

    void QoiView::callback_error(int error, const char* description)
    {
        fmt::println(stderr, "GLFW Error [{:#010x}]: {}", error, description);
    }

    void QoiView::callback_framebuffer_size(GLFWwindow* window, int width, int height)
    {
        auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
        gl::glViewport(0, 0, width, height);
        view.update_aspect(width, height);
    }

    void QoiView::callback_key(GLFWwindow* window, int key, int, int action, int)
    {
        auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
        if (action == GLFW_RELEASE) {
            return;
        }

        switch (key) {
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_Q: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        case GLFW_KEY_H: view.update_offset(Movement::Left); break;
        case GLFW_KEY_L: view.update_offset(Movement::Right); break;
        case GLFW_KEY_J: view.update_offset(Movement::Down); break;
        case GLFW_KEY_K: view.update_offset(Movement::Up); break;
        case GLFW_KEY_I: view.update_zoom(Zoom::In); break;
        case GLFW_KEY_O: view.update_zoom(Zoom::Out); break;
        case GLFW_KEY_F: view.toggle_fullscreen(); break;
        case GLFW_KEY_N: view.toggle_filtering(); break;
        case GLFW_KEY_M: view.toggle_mipmap(); break;
        case GLFW_KEY_R: (view.reset_zoom(), view.reset_offset()); break;
        case GLFW_KEY_P: fmt::println("{}", view.current_file().c_str()); break;
        case GLFW_KEY_UP: view.update_zoom(Zoom::In); break;
        case GLFW_KEY_DOWN: view.update_zoom(Zoom::Out); break;
        case GLFW_KEY_RIGHT: view.file_next(); break;
        case GLFW_KEY_LEFT: view.file_previous(); break;
        }
    }

    void QoiView::callback_cursor(GLFWwindow* window, double xpos, double ypos)
    {
        auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));

        auto x = static_cast<float>(xpos);
        auto y = static_cast<float>(ypos);

        if (view.m_mouse_press) {
            int width, height;
            glfwGetWindowSize(window, &width, &height);

            auto dx = (x - view.m_mouse.x) / static_cast<float>(width);
            auto dy = (view.m_mouse.y - y) / static_cast<float>(height);
            view.increment_offset({ dx, dy });
        }

        view.m_mouse = { x, y };
    }

    void QoiView::callback_mouse_button(GLFWwindow* window, int button, int action, int)
    {
        auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            view.m_mouse_press = action == GLFW_PRESS;
        }
    }

    void QoiView::callback_scroll(GLFWwindow* window, double, double yoffset)
    {
        auto& view = *static_cast<QoiView*>(glfwGetWindowUserPointer(window));
        if (yoffset > 0) {
            view.update_zoom(Zoom::In);
        } else {
            view.update_zoom(Zoom::Out);
        }
    }

    void QoiView::run(int width, int height, Color background)
    {
        auto to_float = [](std::uint8_t c) { return static_cast<float>(c) / 255.0f; };
        gl::glClearColor(to_float(background.r), to_float(background.g), to_float(background.b), 1.0f);

        gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);
        gl::glEnable(gl::GL_BLEND);

        gl::glUseProgram(m_program);
        gl::glBindVertexArray(m_vao);

        gl::glViewport(0, 0, width, height);
        update_aspect(width, height);

        apply_uniform(Uniform::Zoom);
        apply_uniform(Uniform::Offset);

        glfwSwapInterval(1);

        while (not glfwWindowShouldClose(m_window)) {
            gl::glBindTexture(gl::GL_TEXTURE_2D, m_texture);

            if (auto work = m_decoder.get(); work) {
                auto desc   = m_decoder.current()->desc;
                auto format = desc.channels == qoipp::Channels::RGB ? gl::GL_RGB : gl::GL_RGBA;

                gl::glTexSubImage2D(
                    gl::GL_TEXTURE_2D,
                    0,
                    0,
                    static_cast<gl::GLint>(work->start),
                    static_cast<gl::GLsizei>(desc.width),
                    static_cast<gl::GLsizei>(work->count),
                    format,
                    gl::GL_UNSIGNED_BYTE,
                    work->data.data()
                );
                gl::glGenerateMipmap(gl::GL_TEXTURE_2D);
            }

            gl::glClear(gl::GL_COLOR_BUFFER_BIT);
            gl::glDrawElements(gl::GL_TRIANGLES, indices.size(), gl::GL_UNSIGNED_INT, nullptr);

            glfwSwapBuffers(m_window);
            glfwPollEvents();
        }

        m_decoder.stop();
    }

    void QoiView::update_aspect(int width, int height)
    {
        auto image_ratio  = static_cast<float>(m_image_size.x) / static_cast<float>(m_image_size.y);
        auto window_ratio = static_cast<float>(width) / static_cast<float>(height);

        if (image_ratio > window_ratio) {
            m_aspect.x = 1.0f;
            m_aspect.y = window_ratio / image_ratio;
        } else {
            m_aspect.x = image_ratio / window_ratio;
            m_aspect.y = 1.0f;
        }

        apply_uniform(Uniform::Aspect);
        update_title();
    }

    void QoiView::update_zoom(Zoom zoom)
    {
        if (zoom == Zoom::In) {
            m_zoom *= 1.1f;
        } else {
            m_zoom /= 1.1f;
        }

        apply_uniform(Uniform::Zoom);
        update_title();
    }

    void QoiView::update_offset(Movement movement)
    {
        switch (movement) {
        case Movement::Up: m_offset.y += 0.1f / m_zoom; break;
        case Movement::Down: m_offset.y -= 0.1f / m_zoom; break;
        case Movement::Left: m_offset.x -= 0.1f / m_zoom; break;
        case Movement::Right: m_offset.x += 0.1f / m_zoom; break;
        }

        apply_uniform(Uniform::Offset);
    }

    void QoiView::increment_offset(Vec2<> offset)
    {
        m_offset.x -= offset.x / m_aspect.x / m_zoom * 2.0f;
        m_offset.y -= offset.y / m_aspect.y / m_zoom * 2.0f;
        apply_uniform(Uniform::Offset);
    }

    void QoiView::toggle_fullscreen()
    {
        if (glfwGetWindowMonitor(m_window) != nullptr) {
            auto [xpos, ypos]    = m_window_pos;
            auto [width, height] = m_window_size;
            glfwSetWindowMonitor(m_window, nullptr, xpos, ypos, width, height, GLFW_DONT_CARE);
        } else {
            glfwGetWindowPos(m_window, &m_window_pos.x, &m_window_pos.y);
            glfwGetWindowSize(m_window, &m_window_size.x, &m_window_size.y);

            const auto& mode = *m_mode;
            glfwSetWindowMonitor(m_window, m_monitor, 0, 0, mode.width, mode.height, mode.refreshRate);
        }
    }

    void QoiView::toggle_filtering()
    {
        auto count  = static_cast<int>(Filter::count);
        auto filter = static_cast<Filter>((static_cast<int>(m_filter) + 1) % count);
        update_filtering(filter, m_mipmap);
        update_title();
    }

    void QoiView::toggle_mipmap()
    {
        update_filtering(m_filter, not m_mipmap);
        update_title();
    }

    void QoiView::file_next()
    {
        if (m_files.size() == 1) {
            return;
        }

        m_index = (m_index + 1) % m_files.size();

        while (true and not m_files.empty()) {
            m_index = m_index % m_files.size();
            if (prepare_texture()) {
                break;
            } else {
                m_files.erase(m_files.begin() + static_cast<long>(m_index));
            }
        }

        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        update_aspect(width, height);
    }

    void QoiView::file_previous()
    {
        if (m_files.size() == 1) {
            return;
        }

        while (true and not m_files.empty()) {
            m_index = (m_index + m_files.size() - 1) % m_files.size();
            if (prepare_texture()) {
                break;
            } else {
                m_files.erase(m_files.begin() + static_cast<long>(m_index));
            }
        }

        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        update_aspect(width, height);
    }

    void QoiView::reset_zoom()
    {
        m_zoom = 1.0f;
        apply_uniform(Uniform::Zoom);
        update_title();
    }

    void QoiView::reset_offset()
    {
        m_offset = { 0.0f, 0.0f };
        apply_uniform(Uniform::Offset);
    }

    void QoiView::update_title()
    {
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);

        auto window_scale = static_cast<float>(width) / static_cast<float>(m_mode->width);
        auto image_scale  = static_cast<float>(m_image_size.x) / static_cast<float>(m_mode->width);
        auto zoom         = static_cast<int>(m_zoom * 100.0f * window_scale / image_scale * m_aspect.x);

        auto title = fmt::format(
            "[{}/{}] [{}x{}] [{}%] QoiView - {} [filter:{}|mipmap:{}]",
            m_index + 1,
            m_files.size(),
            m_image_size.x,
            m_image_size.y,
            zoom,
            current_file().filename().string(),
            m_filter == Filter::Linear ? "linear" : "nearest",
            m_mipmap ? "yes" : "no"
        );
        glfwSetWindowTitle(m_window, title.c_str());
    }

    void QoiView::prepare_rect()
    {
        gl::glGenVertexArrays(1, &m_vao);
        gl::glBindVertexArray(m_vao);

        gl::glGenBuffers(1, &m_vbo);
        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, m_vbo);
        gl::glBufferData(gl::GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), gl::GL_STATIC_DRAW);

        gl::glGenBuffers(1, &m_ebo);
        gl::glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        gl::glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), gl::GL_STATIC_DRAW);

        gl::glVertexAttribPointer(0, 2, gl::GL_FLOAT, gl::GL_FALSE, 4 * sizeof(float), (void*)0);
        gl::glVertexAttribPointer(
            1, 2, gl::GL_FLOAT, gl::GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float))
        );
        gl::glEnableVertexAttribArray(0);
        gl::glEnableVertexAttribArray(1);

        gl::glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
        gl::glBindVertexArray(0);
    }

    void QoiView::prepare_shader()
    {
        auto buf     = std::array<char, 1024>{};
        auto success = gl::GLint{};

        auto vert = gl::glCreateShader(gl::GL_VERTEX_SHADER);
        gl::glShaderSource(vert, 1, &vertex_shader, nullptr);
        gl::glCompileShader(vert);
        gl::glGetShaderiv(vert, gl::GL_COMPILE_STATUS, &success);

        if (success == false) {
            gl::glGetShaderInfoLog(vert, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to compile vertex shader: {}", buf.data());
            glfwTerminate();
            std::exit(1);
        }

        auto frag = gl::glCreateShader(gl::GL_FRAGMENT_SHADER);
        gl::glShaderSource(frag, 1, &fragment_shader, nullptr);
        gl::glCompileShader(frag);
        gl::glGetShaderiv(frag, gl::GL_COMPILE_STATUS, &success);

        if (success == false) {
            gl::glGetShaderInfoLog(frag, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to compile fragment shader: {}", buf.data());
            glfwTerminate();
            std::exit(1);
        }

        m_program = gl::glCreateProgram();
        gl::glAttachShader(m_program, vert);
        gl::glAttachShader(m_program, frag);
        gl::glLinkProgram(m_program);
        gl::glGetProgramiv(m_program, gl::GL_LINK_STATUS, &success);

        if (success == false) {
            gl::glGetProgramInfoLog(m_program, buf.size(), nullptr, buf.data());
            fmt::println(stderr, "Failed to link shader program: {}", buf.data());
            glfwTerminate();
            std::exit(1);
        }

        gl::glDeleteShader(vert);
        gl::glDeleteShader(frag);
    }

    bool QoiView::prepare_texture()
    {
        const auto& file = current_file();

        assert(fs::exists(file) and fs::is_regular_file(file));

        auto desc = m_decoder.start(file);
        if (not desc) {
            fmt::println(stderr, "Failed to decode file {:?}: {}", file.c_str(), to_string(desc.error()));
            return false;
        }

        if (m_texture != 0) {
            gl::glDeleteTextures(1, &m_texture);
        }

        gl::glGenTextures(1, &m_texture);
        gl::glBindTexture(gl::GL_TEXTURE_2D, m_texture);

        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, gl::GL_CLAMP_TO_EDGE);
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, gl::GL_CLAMP_TO_EDGE);
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR_MIPMAP_LINEAR);
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);

        auto format = desc->channels == qoipp::Channels::RGB ? gl::GL_RGB : gl::GL_RGBA;
        auto width  = static_cast<gl::GLint>(desc->width);
        auto height = static_cast<gl::GLint>(desc->height);
        gl::glTexImage2D(
            gl::GL_TEXTURE_2D, 0, gl::GL_RGBA, width, height, 0, format, gl::GL_UNSIGNED_BYTE, nullptr
        );
        gl::glGenerateMipmap(gl::GL_TEXTURE_2D);

        // TODO: clear texture: https://stackoverflow.com/a/7196109

        gl::glUseProgram(m_program);
        apply_uniform(Uniform::Tex);

        gl::glActiveTexture(gl::GL_TEXTURE0);
        gl::glBindTexture(gl::GL_TEXTURE_2D, m_texture);

        m_image_size = {
            .x = static_cast<int>(desc->width),
            .y = static_cast<int>(desc->height),
        };

        return true;
    }

    void QoiView::update_filtering(Filter filter, bool mipmap)
    {
        m_filter = filter;
        m_mipmap = mipmap;

        if (m_filter == Filter::Linear) {
            auto min = m_mipmap ? gl::GL_LINEAR_MIPMAP_LINEAR : gl::GL_LINEAR;
            gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, min);
            gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
        } else {
            auto min = m_mipmap ? gl::GL_NEAREST_MIPMAP_NEAREST : gl::GL_NEAREST;
            gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, min);
            gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_NEAREST);
        }
    }

    void QoiView::apply_uniform(Uniform uniform)
    {
        auto loc = [this](const char* name) { return gl::glGetUniformLocation(m_program, name); };
        switch (uniform) {
        case Uniform::Zoom: gl::glUniform1f(loc("zoom"), m_zoom); break;
        case Uniform::Offset: gl::glUniform2f(loc("offset"), m_offset.x, m_offset.y); break;
        case Uniform::Aspect: gl::glUniform2f(loc("aspect"), m_aspect.x, m_aspect.y); break;
        case Uniform::Tex: gl::glUniform1i(loc("tex"), 0); break;
        }
    }
}
