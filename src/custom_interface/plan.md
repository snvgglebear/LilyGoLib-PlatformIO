# Interface functionality.
- Phone notifications as "popups" on the screen, with a timeout and a callback function.
- lora/meshtastic functionality (primarily managing the connection from the phone, but also supporting a bluetooth keyboard.)
- Battery status display and low battery warnings.
- alarms, timers, and stopwatch features. 
- wifi management and connection status display.
- make and manage phone calls (accept, reject, and end calls) and display call status.
- navigation functionality, including displaying directions and estimated time of arrival with openstreetmap integration.
- ir remote functionality to control other devices.
Use the existing factory example (and any other examples you find) as a starting point for the implementation of these features with the following modifications:
- copy the existing factory example to a new folder called new_interface and modify it to implement the following changes:
- implement the "safe area" functionality to avoid the rounded corners of the screen. (as defined in custom_interface/usable_area)
- Create a home screen with a clock and battery status, and the ability to pin links to access the other features.
- incorporate the gadgetbridge functionality from custom_interface/gadgetbridge_ble to receive notifications from the phone and display them on the screen.
- I would like an analog and a digital clock option, and the ability to switch between them.
- for notifications, I would like configurable settings for temporaliy displaying notifications on the screen, and for how long they are displayed before being dismissed, as well as whether or not vibration happens. 
# Controlling from phone
- if possible I would like to be able to change watch settings from the phone to the extent possible. 
#gadgetbridge functionality
- any parts of this implementation that need to be done in gadgetbridge app should have an implmentation plan written up that can be implemented in that repo.
# general implementation plan
- Make as much as possible runnable/testable in the simulator, and make sure that the code is well structured and modular to allow for easy testing and debugging.
- All constants and configuration options should be defined in a single location to allow for easy modification.
- The code should be well documented, with clear explanations of the functionality and how to use it.
- make all screens use the custom_interface/usable_area to avoid the rounded corners of the screen, while using all of the available screen space.
- decouple the gadgetbridge functionality from the ui setup, so that it can be tested independently and reused in other parts of the codebase.
