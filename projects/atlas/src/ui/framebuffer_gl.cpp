#include "ui/framebuffer_gl.h"
#include "ui/gl_error.h"
#include <fstream>
#include <iostream>

namespace atlas {

FramebufferGL::FramebufferGL(int width, int height)
{
    // Generate texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create full screen quad
    // For whatever reason, OpenGL doesnt like GL_QUADS (gives me no output), so I'll just use two triangles
    // NOTE: vertical texture coordinates are swapped
    // clang-format off
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 0.0f,

        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 0.0f
    };
    // clang-format on
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // Load shader
    {
        GLuint vertexShader = loadShader("atlas_output.vs", GL_VERTEX_SHADER);
        GLuint fragmentShader = loadShader("atlas_output.fs", GL_FRAGMENT_SHADER);

        m_shader = glCreateProgram();
        glAttachShader(m_shader, vertexShader);
        glAttachShader(m_shader, fragmentShader);
        glLinkProgram(m_shader);

        glDetachShader(m_shader, vertexShader);
        glDetachShader(m_shader, fragmentShader);
    }
}

FramebufferGL::~FramebufferGL()
{
    glDeleteTextures(1, &m_texture);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteProgram(m_shader);
}

#define BUFFER_OFFSET(i) ((char*)NULL + (i))

void FramebufferGL::update(Sensor& sensor, float multiplier)
{
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(m_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    auto frameBuffer = sensor.copyFrameBufferVec3();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, sensor.getResolution().x, sensor.getResolution().y, 0, GL_RGB, GL_FLOAT, frameBuffer.data());
    glUniform1i(glGetUniformLocation(m_shader, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(m_shader, "u_multiplier"), multiplier);

    glBindVertexArray(m_vao);

    glEnable(GL_FRAMEBUFFER_SRGB);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

GLuint FramebufferGL::loadShader(std::string_view fileName, GLenum type)
{
    std::ifstream file = std::ifstream(std::string(fileName));
    if (!file.is_open()) {
        std::string errorMessage = "Cannot open file: ";
        errorMessage += fileName;
        std::cout << errorMessage.c_str() << std::endl;
    }
    std::string source(std::istreambuf_iterator<char>(file), (std::istreambuf_iterator<char>()));

    GLuint shader = glCreateShader(type);
    const char* sourcePtr = source.data();
    GLint sourceSize = (GLint)source.length();
    glShaderSource(shader, 1, &sourcePtr, &sourceSize);
    glCompileShader(shader);

    GLint shaderCompiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompiled);
    if (!shaderCompiled) {
        std::cout << "Error compiling " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << " shader" << std::endl;
        int infologLength = 0;
        int charsWritten = 0;
        char* infoLog;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infologLength);
        if (infologLength > 0) {
            infoLog = new char[infologLength];
            glGetShaderInfoLog(shader, infologLength, &charsWritten, infoLog);
            std::cout << infoLog << std::endl;
            delete infoLog;
        }
    }
    return shader;
}
}
