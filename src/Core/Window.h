#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Window
{
public:
    Window(uint32_t width, uint32_t height, const char* title);
    ~Window();

    void PollEvents();

    bool ShouldClose() const;

    GLFWwindow* GetHandle() const;

private:
    GLFWwindow* m_Window;
};#pragma once
