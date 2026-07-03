#include "Window.h"

#include <stdexcept>

Window::Window(uint32_t width,
    uint32_t height,
    const char* title)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API,
        GLFW_NO_API);

    m_Window =
        glfwCreateWindow(
            width,
            height,
            title,
            nullptr,
            nullptr);

    if (!m_Window)
        throw std::runtime_error("Create Window Failed");
}

Window::~Window()
{
    glfwDestroyWindow(m_Window);
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