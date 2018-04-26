#include <Servo.h>

#define MIN 500
#define MAX 2500

class limits_t {
  public:
    uint16_t min = 1000;
    uint16_t zero = 1500;
    uint16_t max = 2000;

    limits_t() {}
    
    bool checkLimits() {
      return ((min >= MIN) && (min <= MAX) &&
              (zero >= MIN) && (zero <= MAX) &&
              (max >= MIN) && (max <= MAX)); 
    }

  private:
};

class newServo: public Servo {

  public:
    limits_t limits;

    uint8_t pin;
    int16_t currentValue = 0;
    int16_t targetValue = 0;
    uint16_t ms;

    uint8_t rateLimit = 0;  
    bool done;

    newServo() {}
    
    newServo(byte _pin, byte _rateLimit) {
      pin = _pin;
      rateLimit = _rateLimit;
    }

    void connect(bool userConnected) {
      if(userConnected) {
        Servo::attach(pin);
        writeMicroseconds(limits.zero);
      }
      else 
        detach();
    }

    //Move the servo towards the desired value at a speed determined by the rate limit
    bool update(){
      //Work out if the servo needs to move at all
      done = (abs(targetValue - currentValue) <= rateLimit);

      //Serial.printf("Pin: %d, Target: %d, Current: %d, Done: %d\n", pin, targetValue, currentValue, done);
      
      if(!done) {
        if(rateLimit == 0)  {
          currentValue = targetValue;
        }
        else {
          if(targetValue > currentValue)    currentValue += rateLimit;
          if(targetValue < currentValue)    currentValue -= rateLimit;          
        }
    
        writeMicroseconds(mapValue(currentValue));
      }
      else if(currentValue != targetValue) {
        currentValue = targetValue;
        writeMicroseconds(mapValue(currentValue));
      }

      return done;
    }


    //Map the input Value (+/- 100% into the servo ms value
    uint16_t mapValue(long value){
      const uint16_t MAXVALUE = 1000;
      uint16_t range; 

      if(value < 0)
        range = limits.zero - limits.min;
      else
        range = limits.max - limits.zero;
      
      //Limit the input value
      if(value > MAXVALUE)  value = MAXVALUE;
      if(value < -MAXVALUE) value = -MAXVALUE;
      
      //Map the +- 1000 range down to the servo microseconds
      ms =  ((value * range) / MAXVALUE) + limits.zero;

      //Limit the output ms
      if(ms < limits.min) ms = limits.min;
      if(ms > limits.max) ms = limits.max;
      return ms;
    }
};

