Import("env")
import os

# ESP32 Arduino 3.x added a "Network" library that WiFi and other libraries
# depend on, but WiFi/library.properties doesn't declare this dependency,
# so PlatformIO's LDF doesn't add Network/src to the WiFi compilation.
# This script adds it to fix "fatal error: Network.h: No such file or directory".
framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if framework_dir:
    network_include = os.path.join(framework_dir, "libraries", "Network", "src")
    if os.path.isdir(network_include):
        env.Prepend(CPPPATH=[network_include])
