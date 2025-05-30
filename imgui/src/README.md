# How to Build

-   On Windows with Visual Studio's IDE

Use the provided project file (.vcxproj). Add to solution (imgui_examples.sln) if necessary.

-   On Windows with Visual Studio's CLI

```
set SDL2_DIR=path_to_your_sdl2_folder
cl /Zi /MD /I.. /I..\.. /I%SDL2_DIR%\include main.cpp ..\..\backends\imgui_impl_sdl2.cpp ..\..\backends\imgui_impl_opengl2.cpp ..\..\imgui*.cpp /FeDebug/example_sdl2_opengl2.exe /FoDebug/ /link /libpath:%SDL2_DIR%\lib\x86 SDL2.lib SDL2main.lib opengl32.lib /subsystem:console
#          ^^ include paths                  ^^ source files                                                            ^^ output exe                    ^^ output dir   ^^ libraries
# or for 64-bit:
cl /Zi /MD /I.. /I..\.. /I%SDL2_DIR%\include main.cpp ..\..\backends\imgui_impl_sdl2.cpp ..\..\backends\imgui_impl_opengl2.cpp ..\..\imgui*.cpp /FeDebug/example_sdl2_opengl2.exe /FoDebug/ /link /libpath:%SDL2_DIR%\lib\x64 SDL2.lib SDL2main.lib opengl32.lib /subsystem:console
```

-   On Linux and similar Unixes

```
<<<<<<< Updated upstream
c++ -std=c++17 `sdl2-config --cflags` -I .. -I ../ -I backends src/main.cpp src/Application.cpp src/filemanager.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl2.cpp imgui*.cpp `sdl2-config --libs` -lGL
=======
c++ -std=c++17 `sdl2-config --cflags` -I .. -I ../ -I backends src/main.cpp src/Application.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl2.cpp imgui*.cpp `sdl2-config --libs` -lGL
>>>>>>> Stashed changes
```

-   On Mac OS X

```
c++ -std=c++17 `sdl2-config --cflags` -I .. -I ../ -I backends src/main.cpp src/Application.cpp src/filemanager.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl2.cpp imgui*.cpp `sdl2-config --libs` -framework OpenGl
```
