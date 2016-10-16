Project based on https://github.com/davidgfnet/wifi_display, and most parts of code here are David's.

More details you can find in an article: https://davidgf.net/page/41/e-ink-wifi-display



# Changes:
* Using UART with default display's firmware: no changes to display required. Because of this change this firmware is not applicable for batteries — it's up around 45 seconds when drawing about 30% of the screen.
* Using default display's sleep&wakeup commands
