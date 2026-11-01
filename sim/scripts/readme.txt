# Screenshots for the README (resources/), in the current theme: put "theme ID" in a script first.
# Scrolling text is slowed almost to a stop so every text shows from its start.
scroll 1
click                                           # wakes the screen on the launcher
shot launcher
click; click; hold 700; wait 300; shot settings # Settings, opened from the launcher
click; wait 300; hold 700; wait 300; shot choice    # Layout
double; wait 300                                # back on Layout
click; click; click; click; click; wait 300     # to About
hold 700; wait 300; shot about
double; wait 300; click; wait 300               # back on About, then Factory reset
hold 700; wait 300; shot confirm
double; wait 300                                # back in Settings: overlays show over it
fido sign_in github.com; wait 300
press; wait 1100; shot security-key
release; wait 100; fido_done ok; wait 300; shot approved
wait 2000
pairing 482915; wait 300; shot pairing
pairing off; wait 300
typing on; wait 300; shot typing
typing off; wait 300
