#pragma once

// Main-loop decisions for handing the SD card between the recorder and the USB
// host; plain logic, the caller turns the actions into calls. Per loop:
//   a = handover.step(own.hostOwns(), core.mode() == Usb, [] { return usbsd::release(); });
// then, in this order: grant -> usbsd::grant(), usbOn -> core.requestUsb(true),
// park -> writer.park(), unpark -> writer.unpark(), usbOff -> core.requestUsb(false).
//
// Rules: while the host owns the card the USB gate is granted on every loop
// (a revoke left over from an unfinished release must not outlive a host that
// took the card back; nothing is unparked in that state, so it is safe). The
// writer is parked once the core is in Usb mode. When the host lets go,
// release() is retried on each loop until no transfer is running; only then is
// the writer unparked (if it was parked) and Usb mode left.
class CardHandover {
public:
  struct Actions {
    bool grant = false, usbOn = false, park = false, unpark = false, usbOff = false;
  };

  template <class Release> Actions step(bool hostOwns, bool coreUsb, Release release) {
    Actions a;
    if (hostOwns) {
      a.grant = true;
      if (!usbWanted_) {
        usbWanted_ = true;
        a.usbOn = true;
      }
      if (!parkRequested_ && coreUsb) {
        parkRequested_ = true;
        a.park = true;
      }
    } else if (usbWanted_ && release()) {
      a.unpark = parkRequested_;
      a.usbOff = true;
      usbWanted_ = parkRequested_ = false;
    }
    return a;
  }

private:
  bool usbWanted_ = false, parkRequested_ = false;
};
