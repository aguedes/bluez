#!/usr/bin/python
import sys
import dbus
import dbus.service
import dbus.mainloop.glib
import gobject

StateIncoming = 0
StateActive = 3
StateEnded = 6

ALERT_RINGER_STATE = 1 << 0

on_call = False
saved_ringer_setting = None
ringer_setting = None
silent_mode = False
alert_status = 0x00

def set_ringer(value):
    global ringer_setting
    ringer_setting = value
    profiled.set_value("", "ringing.alert.type", value, dbus_interface="com.nokia.profiled")

def get_ringer():
    return profiled.get_value("", "ringing.alert.type", dbus_interface="com.nokia.profiled")

def set_alert_status():
    global ringer_setting, alert_status, on_call

    old_alert_status = alert_status
    if on_call and ringer_setting != "Silent":
        alert_status |= ALERT_RINGER_STATE
    else:
        alert_status &= ~ALERT_RINGER_STATE

    if old_alert_status != alert_status:
        print "NotifyAlertStatus(%02x)" % alert_status
        phone.NotifyAlertStatus(dbus.Byte(alert_status))

def silence_ringer():
    print "silence_ringer()"
    global saved_ringer_setting, ringer_setting
    saved_ringer_setting = ringer_setting
    set_ringer("Silent")

def restore_ringer():
    global saved_ringer_setting
    if saved_ringer_setting:
        print "Restoring ringer setting to \"%s\"" % saved_ringer_setting
        set_ringer(saved_ringer_setting)
        saved_ringer_setting = None

def call_state_changed(*args, **kwargs):
    global on_call

    for item in args[0]:
        if not isinstance(item, dbus.Dictionary):
            continue
        if item.get("state") is None:
            continue
        if item["state"] == StateIncoming:
            on_call = True
            set_alert_status()
        elif item["state"] in [StateActive, StateEnded]:
            on_call = False
            set_alert_status()
            if not silent_mode:
                restore_ringer()

def profile_changed(*args, **kwargs):
    global ringer_setting
    print "profile_changed()"
    for item in args[3]:
        if item[0] != "ringing.alert.type":
            continue
        print "Ringer: %s" % item[1]
        if item[1] != ringer_setting or silent_mode:
            ringer_setting = item[1]
            print "NotifyRingerSetting(%s)" % ringer_setting
            phone.NotifyRingerSetting(ringer_setting)
            if silent_mode:
                # User has changed profile, update saved setting
                saved_ringer_setting = ringer_setting

class PhoneAgent(dbus.service.Object):
    @dbus.service.method("org.bluez.PhoneAgent",
            in_signature="", out_signature="")
    def MuteOnce(self):
        print "MuteOnce()"

        global silent_mode
        if silent_mode:
            print "In Silent mode"
            return

        global on_call
        if not on_call:
            print "No active call"
            return

        global ringer_setting
        if ringer_setting == "Silent":
            print "Ringer already silent"
            return

        silence_ringer()

    @dbus.service.method("org.bluez.PhoneAgent",
            in_signature="", out_signature="")
    def SetSilentMode(self):
        print "SetSilentMode()"

        global silent_mode
        if silent_mode:
            return

        silence_ringer()
        silent_mode = True

    @dbus.service.method("org.bluez.PhoneAgent",
            in_signature="", out_signature="")
    def CancelSilentMode(self):
        print "CancelSilentMode()"

        global silent_mode
        if not silent_mode:
            return

        silent_mode = False
        restore_ringer()

    @dbus.service.method("org.bluez.PhoneAgent",
            in_signature="", out_signature="s")
    def GetSilentMode(self):
        print "GetSilentMode()"

        global ringer_setting
        return ringer_setting

    @dbus.service.method("org.bluez.PhoneAgent",
            in_signature="", out_signature="y")
    def GetAlertStatus(self):
        print "GetAlertStatus()"

        global alert_status
        return alert_status


if __name__ == "__main__":
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    system_bus = dbus.SystemBus()
    session_bus = dbus.SessionBus()
    agent_path = "/test/phoneagent"

    # Monitor call incoming/ended events
    contextkit = session_bus.get_object("com.nokia.CallUi.Context",
            "/com/nokia/CallUi/ActiveCall")
    contextkit.connect_to_signal("ValueChanged", call_state_changed,
            dbus_interface="org.maemo.contextkit.Property")

    profiled = session_bus.get_object("com.nokia.profiled",
            "/com/nokia/profiled")
    # Monitor profile changes
    profiled.connect_to_signal("profile_changed", profile_changed,
            dbus_interface="com.nokia.profiled")
    ringer_setting = get_ringer()

    set_alert_status()

    phone = dbus.Interface(system_bus.get_object("org.bluez",
            "/test/phonealert"), "org.bluez.PhoneAlert")

    agent = PhoneAgent(system_bus, agent_path)
    phone.RegisterAgent(agent_path)
    print "destination:", system_bus.get_unique_name()

    mainloop = gobject.MainLoop()
    mainloop.run()
