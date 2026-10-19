#include "Window.h"

#include <stdexcept>

Window::Window(uint32_t width, uint32_t height, const char* title)
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);

    if (!m_Window)
    {
        throw std::runtime_error("Failed to create GLFW window.");
    }
}

Window::~Window()
{
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
}

void Window::PollEvents()
{
    glfwPollEvents();
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(m_Window);
}

GLFWwindow* Window::GetHandle() const
{
    return m_Window;
}