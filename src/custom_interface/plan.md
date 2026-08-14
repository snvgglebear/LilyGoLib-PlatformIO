# Interface functionality.
- Phone notifications as "popups" on the screen, with a timeout and a callback function.
- lora/meshtastic functionality (primarily managing the connection from the phone, but also supporting a bluetooth keyboard.)
- Battery status display and low battery warnings.
- alarms, timers, and stopwatch features. 
- wifi management and connection status display.
# Existing Factory example
This example has most of the functionality that I want with the following changes:
- copy the existing factory example to a new folder called new_interface and modify it to implement the following changes:
- implement the "safe area" functionality to avoid the rounded corners of the screen. (as defined in custom_interface/usable_area)
- Create a home screen with a clock and battery status, and the ability to pin links to access the other features.
- incorporate the gadgetbridge functionality from custom_interface/gadgetbridge_ble to receive notifications from the phone and display them on the screen.
- I would like an analog and a digital clock option, and the ability to switch between them.
# Controlling from phone