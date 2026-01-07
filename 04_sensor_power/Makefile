APPLICATION = 04_sensor_power

BOARD ?= seeedstudio-xiao-nrf52840

RIOTBASE ?= $(CURDIR)/../RIOT

FEATURES_REQUIRED += periph_i2c
FEATURES_OPTIONAL += periph_i2c_reconfigure

# Timer
USEMODULE += ztimer
USEMODULE += ztimer_msec
USEMODULE += ztimer_sec

# If you use puts/printf during testing, keep stdio:
USEMODULE += stdio

# (Optional) Helps reduce CPU usage while waiting when adding ztimer_sleep in polling loops
# USEMODULE += ztimer_usec

DEVELHELP ?= 1
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
