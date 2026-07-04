#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>

class Window
{
public:
    Window(uint32_t width, uint32_t height, const char* title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void PollEvents();
    bool ShouldClose() const;

    GLFWwindow* GetHandle() const;

private:
    GLFWwindow* m_Window = nullptr;
};