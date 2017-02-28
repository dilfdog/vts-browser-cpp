#ifndef MAINWINDOW_H_wuiegfzbn
#define MAINWINDOW_H_wuiegfzbn

#include "gpuContext.h"

class GLFWwindow;

namespace melown
{
class MapFoundation;
} // namespace

class MainWindow
{
public:
    MainWindow();
    ~MainWindow();

    void mousePositionCallback(double xpos, double ypos);
    void mouseScrollCallback(double, double yoffset);

    void run();
    void showFps();

    double mousePrevX, mousePrevY;
    melown::MapFoundation *map;
    GLFWwindow* window;
    GpuContext gpu;

    double lastFrameTime;
};

#endif