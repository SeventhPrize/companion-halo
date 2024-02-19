class TouchHandler {
  /*
  This class handles readings by ESP32 touch sensor.
  Encapsulates methods for interpreting the touch readings.
  */

private:
  byte pin;                         // Touch sensor pin
  byte touchThreshold;              // Threshold for considering a touch reading as a positive
  unsigned long lastPush = 0;       // ms timestamp of the last time the sensor became positive from negative
  unsigned long lastLift = 0;       // ms timestamp of the last time the sensor became negative from positive
  unsigned long lastTouch = 0;      // ms timestamp of the last time the sensor read positive
  unsigned long lastUntouch = 0;    // ms timestamp of the last time the sensor read negative
  unsigned long lastHoldDur = 0;    // ms duration of the last sensor hold
  unsigned long lastUnholdDur = 0;  // ms duration of the last sensor unhold

public:
  TouchHandler(byte inputPin, byte inputTouchThreshold = 35) {
    /*
    Initializes this object
    INPUT
      inputPin; touch sensor pin
      inputTouchThreshold; the threshold for considering a touch reading as a positive
    */
    setPin(inputPin);
    setTouchThreshold(inputTouchThreshold);
  }

  void setTouchThreshold(byte val) {
    /*
    Sets the touch threshold to given val
    */
    touchThreshold = val;
  }

  void setPin(byte val) {
    /*
    Sets the touch sensor pin to the given val
    */
    pin = val;
  }

  byte getReading() {
    /*
    Returns the touch sensor's current reading
    */
    return touchRead(pin);
  }

  bool isOneTouch() {
    /*
    Returns whether the touch sensor is currently positive or negative
    */
    return (getReading() < touchThreshold);
  }

  bool isThreeTouch() {
    /*
    Returns whether the touch sensor is more positive or more negative out of three readings
    */
    bool touch = isOneTouch();
    if (touch != isOneTouch()) {
      touch = isOneTouch();
    }
    return touch;
  }

  bool isTouch() {
    /*
    Samples three readings from the sensor and returns whether the majority is positive or negative
    */

    // Get touch sensor readings
    bool touch = isThreeTouch();

    // Record reading data into class fields
    unsigned long now = millis();
    if (touch) {
      lastTouch = now;
      lastHoldDur = now - lastUntouch;
      if (lastPush <= lastLift) {
        lastPush = now;
      }
    } else {
      lastUntouch = now;
      lastUnholdDur = now - lastTouch;
      if (lastPush > lastLift) {
        lastLift = now;
      }
    }

    // Return reading
    return touch;
  }

  byte isTouchDetailed(unsigned long hold_duration = 0) {
    /*
    INPUT
      hold_duration: ms duration of a contiuous touch to count as a Hold
    Returns
      0: No change
      1: Click (was untouched, now touched)
      2: Unclick (was touched, now untouched)
      3: Hold (has been continously touched for hold_duration ms)
      4: Unhold (has been continously untouched for any amount of time)
    */

    // Get touch sensor readings
    bool touch = isThreeTouch();
    unsigned long now = millis();
    byte returnVal = 0;

    if (touch) {
      // If was untouched and is now touched, return click
      if (lastTouch <= lastUntouch) {
        lastPush = now;
        returnVal = 1;
      }
      // If was already being touched and is still bring touched, return hold
      else if (now - lastPush >= hold_duration) {
        returnVal = 3;
      }
      lastTouch = now;
      lastHoldDur = now - lastUntouch;
    } else {
      // If was touched and is now untouched, return unclick
      if (lastTouch > lastUntouch) {
        lastLift = now;
        returnVal = 2;
      }
      // If was already not being touched and is still not being touched, return unhold
      else {
        returnVal = 4;
      }
      lastUntouch = now;
      lastUnholdDur = now - lastTouch;
    }
    return returnVal;
  }

  bool isHold() {
    /*
    Returns whether the sensor is currently held positive (held down)
    */
    return (lastPush > lastLift);
  }

  bool isUnhold() {
    /*
    Returns whether the sensor is currently held negative (not held down)
    */
    return (lastPush <= lastLift);
  }

  unsigned long getHoldDur() {
    /*
    Returns the duration of the current sensor hold
    */
    if (isHold()) {
      return lastTouch - lastPush;
    }
    return 0;
  }

  unsigned long getUnholdDur() {
    /*
    Returns the duration of the current sensor unhold
    */
    if (isUnhold()) {
      return lastUntouch - lastLift;
    }
    return 0;
  }

  unsigned long getLastHoldDur() {
    /*
    Returns the duration of the sensor hold from the last time the sensor was being touched
    */
    return lastHoldDur;
  }

  unsigned long getLastUnholdDur() {
    /*
    Returns the duration of the sensor unhold from the last time the sensor was not being touched
    */
    return lastUnholdDur;
  }

  unsigned long getLastPush() {
    /*
    Returns the ms timestamp of the last time the sensor became positive from negative
    */
    return lastPush;
  }

  unsigned long getLastLift() {
    /*
    Returns the ms timestamp of the last time the sensor became negative from positive
    */
    return lastLift;
  }

  unsigned long getLastTouch() {
    /*
    Returns the ms timestamp of the last time the sensor read positive
    */
    return lastTouch;
  }

  unsigned long getLastUntouch() {
    /*
    Returns the ms timestamp of the last time the sensor read negative
    */
    return lastUntouch;
  }

  unsigned long getLastActivity() {
    /*
    Returns the last time the sensor flipped signals--i.e., became positive from negative or became negative from positive
    */
    return max(lastPush, lastLift);
  }
};