# IoT

Code for my custom Home Automation devices.

### Directories & Files:

- ./clients/: C++ code for devices based on ESP32/8266 modules, deployed using platform.io.
- ./servers/: Python servers run on a system (Linux) on local network.
- ./servers/base_service_models.py: Base class that establishes a service layer over the MQTT communication protocol. It automatically starts/stops services based on requirement.
- ./start_services.py: Central script that is supposed to launch all the services once in the beginning.

TODO: Upload and add a link to 3D models for devices printed using a 3D-printer.
