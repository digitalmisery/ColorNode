//*********************************************************************
//* ColorNode Tree Software 
//* by Paul Martis
//* http://www.digitalmisery.com
//* November 18, 2012
//*********************************************************************
//Library Includes
#include <JeeLib.h>
#include <G35.h>
#include <Orbiter.h>
#include <Worm.h>
//*********************************************************************
//Hardware Definitions
#define OUT_PIN 19  //Arduino pin #
#define LIGHT_COUNT 50  //Total # of lights on string
#define LAST_LIGHT (LIGHT_COUNT-1)
#define HALFWAY_POINT (LIGHT_COUNT/2)

// One bulb's share of a second, in milliseconds
#define BULB_FRAME (1000/LIGHT_COUNT)

//Wireless Definitions
#define NODE_NUM 12  //This node number (1-30)
#define ACK_NODE 0  //Set to 1 if this node is to ACK on broadcasts
#define CONTROL_NODE 30  //Node ID of the controller
#define FREQ RF12_915MHZ
#define GROUP_NUM 1
#define BROADCAST_RANGE 0xFF //To limit nodes reponse to broadcasts

//To deal with re-sending of packets
#if RETRIES < 10
#undef RETRIES
#define RETRIES 10  // stop retrying after 8 times
#endif
#if RETRY_MS > 10
#undef RETRY_MS
#define RETRY_MS 10  // resend packet every 10ms until ack'ed
#endif

G35 lights(OUT_PIN, LIGHT_COUNT);

int lightShow = 0;  //Light show = 1 when string is on
unsigned long time = 0;
int durations = 0;  //Variable to hold duration of fades/chases.
int lightSet = 0;

static unsigned long now () {  //Function to return seconds elapsed
    return millis() / 1000;
}
#define STAR_BULB 24
/*********************************************************************
GE ColorEffects Tree LED Numbers

         24
      25    23
    26 27  21 22
     28      20
    29        19
   30 31 16 17 18
     32      15
    33        14
   34 35 11 12 13
     36      10
    37         9
  38 39 40  6 7 8
     41      5
    42        4
   43          3
  44            2
45 46 47 48 49 0 1

*/
/*********************************************************************
// Command modes
// 0 = off
// 1 = program (pre-defined program)
//      option # - select program
//      duration # - how long to run program (s)
// 2 = fill (bulbs start to end to same color/brightness)
//      option 1: strobe
//      duration # - strobe rate (ms)
// 3 = fade (to new color and brightness)
//      duration # - time between color/brightness steps (ms)
//      option 0: in from black 1: out to black
// 4 = chase (from start bulb to end bulb w/ color)
//      option 0: up, 1: down, 2: fill up, 3: clear down
//      duration # - chase delay between steps (ms)
// 5 = random (from start bulb to end bulb)
//      option # - how many random bulbs at same time
//      duration # - delay between changing bulbs (ms)
//      if color = 0,0,0 - choose random color each time
//      if intensity = 0 - choose random brightness each time
// 6 = dissolve (from start color to black randomly)
//      option # - how many random bulbs 
//      duration # - dissolve speed (ms)
// Note: if bulb_num = 63, brightness/color setting will change all */

typedef struct {
    uint8_t dest;
    uint8_t mode;
    uint8_t start_bulb;
    uint8_t end_bulb;
    color_t bulb_color;
    uint8_t bulb_intensity; 
    uint8_t option;
    uint8_t duration;
} payload;  //Matches payload sent from controller

payload command;
payload myCommand;

//*********************************************************************
//* For sending commands via serial port

static char cmd;
static byte value, stack[RF12_MAXDATA], top, sendLen, dest, quiet;
static byte testbuf[RF12_MAXDATA], testCounter;

static void addCh (char* msg, char c) {
    byte n = strlen(msg);
    msg[n] = c;
}

static void addInt (char* msg, word v) {
    if (v >= 10)
        addInt(msg, v / 10);
    addCh(msg, '0' + v % 10);
}

char helpText1[] PROGMEM =
    "\n"
    "Command modes" "\n"
    "0 = Off" "\n"
    "1 = Program (pre-defined program)" "\n"
    "    option # - select program" "\n"
    "    duration # - how long to run program (s)" "\n"
    "2 = Fill (bulbs start to end to same color/brightness)" "\n"
    "    option 1: strobe" "\n"
    "    duration # - strobe rate (ms)" "\n"
    "3 = Fade (to new color and brightness)" "\n"
    "    duration # - time between color/brightness steps (ms)" "\n"
    "    option 0: in from black 1: out to black" "\n"
    "4 = Chase (from start bulb to end bulb w/ color)" "\n"
    "    option 0: up, 1: down, 2: fill up, 3: clear down" "\n"
    "    duration # - chase delay between steps (ms)" "\n"
    "5 = Random (from start bulb to end bulb)" "\n"
    "    option # - how many random bulbs at same time" "\n"
    "    duration # - delay between changing bulbs (ms)" "\n"
    "    if color = 0,0,0 - choose random color each time" "\n"
    "    if intensity = 0 - choose random brightness each time" "\n"
    "6 = Dissolve (from start color to black randomly)" "\n"
    "    option # - how many random bulbs" "\n" 
    "    duration # - dissolve speed (ms)" "\n"
    "\n"
    "  <m>=mode <d>=duration, <o>=option, <i>=intensity" "\n"
    "  <R>=Red, <G>=Green, <B>=blue, <s>=start bulb, <e>=end bulb" "\n"
    "\n"
    "  <m>,<s>,<e>,<R>,<G>,<B>,<i>,<o>,<d> l - light command" "\n"
    "  o           - turns off all bulbs and ends any program" "\n"
;

static void showString (PGM_P s) {
    for (;;) {
        char c = pgm_read_byte(s++);
        if (c == 0)
            break;
        if (c == '\n')
            Serial.print('\r');
        Serial.print(c);
    }
}

static void showHelp () {
    showString(helpText1);
    Serial.println("Current configuration:");
    Serial.print("NodeID: ");
    Serial.print(NODE_NUM);
    Serial.print(" Freq: ");
    Serial.print(FREQ);
    Serial.print(" Group: ");
    Serial.println(GROUP_NUM);
}

static void handleInput (char c) {  //Serial control
    if ('0' <= c && c <= '9')
        value = 10 * value + c - '0';
    else if (c == ',') {
        if (top < sizeof stack)
            stack[top++] = value;
        value = 0;
    } else if ('a' <= c && c <='z') {
        Serial.print("> ");
        Serial.print((int) value);
        Serial.println(c);
        switch (c) {
            default:
                showHelp();
                break;
            case 'l': //Light Command
                  myCommand.duration = value;
                  myCommand.option = stack[top-1];
                  myCommand.bulb_intensity = stack[top-2];
                  myCommand.bulb_color = lights.color(stack[top-5], stack[top-4], stack[top-3]);
                  myCommand.end_bulb = stack[top-6];
                  myCommand.start_bulb = stack[top-7];
                  myCommand.mode = stack[top-8];
                  lightShow = 1;
                  lightSet = 0;
                break;
            case 'o': //Off
                  myCommand.mode = 0;
                  lightShow = 0;
                  lightSet = 0;
                break;
        }
        value = top = 0;
        memset(stack, 0, sizeof stack);
    } 
    else if (c > ' ')
        showHelp();
}

//*********************************************************************
//* Setup Function

void setup() {
  Serial.begin(57600);
  Serial.print("\n[Color Node V1]");
  
  lights.enumerate_forward();  //Required for individually addressing of bulbs
  lights.fill_color(0,LIGHT_COUNT,0,COLOR_BLACK);
  myCommand.mode = 1;
  myCommand.option = 26;
  myCommand.bulb_intensity = 1;
  lightShow = 1;
  randomSeed(analogRead(0));
  
  rf12_initialize(NODE_NUM, FREQ, GROUP_NUM);
  rf12_control(0xC647); //Slows down RF data rate to 4.8kbps
  showHelp();  //Display Serial Control message
}

//*********************************************************************
// Interface for light programs - Sowbug and MEO programs added

class LightProgram {
  public:
    virtual void Init() {}
    virtual void Init(uint8_t pattern) {}
    virtual void Do() = 0;
};
//////////////////////////////////////////
class SteadyWhite : public LightProgram {
  public:
    void Init() {
      lights.fill_color(0, LIGHT_COUNT, 0, COLOR_WHITE);
      lights.fade_in(10);
    }
    void Do() {
    }
};
//////////////////////////////////////////
class SteadyMulti : public LightProgram {
  public:
    void Init() {
      for (int i=0; i<LIGHT_COUNT; i=i+5){
        lights.set_color(i, 0, COLOR_RED);
        lights.set_color(i+1, 0, COLOR_GREEN);
        lights.set_color(i+2, 0, COLOR_ORANGE);
        lights.set_color(i+3, 0, COLOR_BLUE);
        lights.set_color(i+4, 0, COLOR_YELLOW);
      }        
    lights.fade_in(10);
    }
    void Do() {
    }
};
//////////////////////////////////////////
class CrossOverWave : public LightProgram {
  public:
    void Init() {
      x_ = LIGHT_COUNT;
    }

    void Do() {
      if (x_ == LIGHT_COUNT) {
        x_ = 0;
        color_a_ = 0;
        color_b_ = 0;
        while (color_a_ == color_b_) {
          color_a_ = G35::max_color(rand());
          color_b_ = G35::max_color(rand());
        }
      }
      lights.set_color(x_, G35::MAX_INTENSITY, color_a_);
      lights.set_color(LAST_LIGHT - x_, G35::MAX_INTENSITY, color_b_);
      ++x_;
      delay(BULB_FRAME);
    }
  private:
    uint8_t x_;
    color_t color_a_;
    color_t color_b_;
};
//////////////////////////////////////////
class ForwardWave : public LightProgram {
  public:
    void Init() {
      x_ = LIGHT_COUNT;
    }

    void Do() {
      if (x_ == LIGHT_COUNT) {
        x_ = 0;
        int old_color = color_;
        do {
          color_ = G35::max_color(rand());
        } while (old_color == color_);
      }
      lights.set_color(x_, G35::MAX_INTENSITY, color_);
      ++x_;
      delay(BULB_FRAME);
    }
  private:
    uint8_t x_;
    color_t color_;
};
//////////////////////////////////////////
class ChasingRainbow : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY,
			 G35::rainbow_color);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
class DoubleRainbow : public LightProgram {
 public:
  void Init() {
  }
  void Do() {
    for(i=0;i<LIGHT_COUNT/2;i++) {
      color_t color = lights.color_hue((i+c)%(HUE_MAX+1));
      lights.set_color(i,G35::MAX_INTENSITY,color);
      lights.set_color(LIGHT_COUNT-1-i,G35::MAX_INTENSITY,color);
    }
    c++;
  }
 private:
  uint8_t i;
  uint16_t c;
};
//////////////////////////////////////////
class AlternateDirectionalWave : public LightProgram {
 public:
  void Init() {
    x_ = LIGHT_COUNT;
    direction_ = -1;
  }

  void Do() {
    bool hit_end = false;
    if (x_ == LIGHT_COUNT) {
      x_ = 0;
      hit_end = true;
    } else if (x_ == 0) {
      x_ = LIGHT_COUNT;
      hit_end = true;
    }
    if (hit_end) {
      direction_ = -direction_;
      int old_color = color_;
      do {
	color_ = G35::max_color(rand());
      } while (old_color == color_);
      delay(500);
    }
    lights.set_color(x_, G35::MAX_INTENSITY, color_);
    x_ += direction_;
    delay(BULB_FRAME);
  }
 private:
  uint8_t x_;
  int8_t direction_;
  color_t color_;
};
//////////////////////////////////////////
class FadeInFadeOutSolidColors : public LightProgram {
 public:
  void Do() {
    int new_color = color_;
    do {
      color_ = G35::max_color(rand());
    } while (new_color == color_);

    lights.fill_color(0, LIGHT_COUNT, 0, color_);
    lights.fade_in(5);
    lights.fade_out(5);
  }
 private:
  color_t color_;
};
//////////////////////////////////////////
class BidirectionalWave : public LightProgram {
 public:
  void Init() {
    x_ = HALFWAY_POINT;
  }

  void Do() {
    // With 50 lights, we run into some edge cases because 50 isn't evenly
    // divisible by 4. It's a fairly crazy program to start with, so I'm
    // leaving it like this.
    if (x_ == HALFWAY_POINT) {
      x_ = 0;
      do {
	color_a_ = G35::max_color(rand());
	color_b_ = G35::max_color(rand());
      } while (color_a_ == color_b_);
      do {
	color_c_ = G35::max_color(rand());
	color_d_ = G35::max_color(rand());
      } while (color_c_ == color_d_);
    }
    lights.set_color(x_, G35::MAX_INTENSITY, color_a_);
    lights.set_color(HALFWAY_POINT - 1 - x_, G35::MAX_INTENSITY, color_b_);
    lights.set_color(HALFWAY_POINT + x_, G35::MAX_INTENSITY, color_c_);
    lights.set_color(LAST_LIGHT - x_, G35::MAX_INTENSITY, color_d_);
    ++x_;
    delay(BULB_FRAME);
  }
 private:
  uint8_t x_;
  color_t color_a_;
  color_t color_b_;
  color_t color_c_;
  color_t color_d_;
};
//////////////////////////////////////////
class ChasingSolidColors : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY,
			 G35::max_color);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
class FadeInFadeOutMultiColors : public LightProgram {
 public:
  void Do() {
    lights.fill_sequence(rand(), 5, 0, G35::max_color);
    lights.fade_in(5);
    lights.fade_out(5);
  }
};
//////////////////////////////////////////
class ChasingTwoColors : public LightProgram {
 public:
  void Init() {
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(sequence_, LIGHT_COUNT / 2, G35::MAX_INTENSITY,
			 G35::rainbow_color);
    sequence_ += LIGHT_COUNT / 2;
    delay(500);
  }
 private:
  uint16_t sequence_;
};
//////////////////////////////////////////
class RandomSparkling : public LightProgram {
 public:
  void Do() {
    lights.fill_random_max(0, LIGHT_COUNT, G35::MAX_INTENSITY);
    delay(1000);
    lights.fill_color(0, LIGHT_COUNT, G35::MAX_INTENSITY, COLOR_BLACK);
    delay(500);
  }
 private:
  color_t color_;
};
//////////////////////////////////////////
class ChasingMultiColors : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY,
			 G35::max_color);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
color_t red_white_blue(uint16_t sequence) {
  sequence = sequence % 3;
  if (sequence == 0) {
    return COLOR_RED;
  }
  if (sequence == 1) {
    return COLOR_WHITE;
  }
  return COLOR_BLUE;
}
//////////////////////////////////////////
color_t red_white_green(uint16_t sequence) {
  sequence = sequence % 3;
  if (sequence == 0) {
    return COLOR_RED;
  }
  if (sequence == 1) {
    return COLOR_WHITE;
  }
  return COLOR_GREEN;
}
class ChasingRedWhiteGreen : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 3, G35::MAX_INTENSITY,
			 red_white_green);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
class ChasingWhiteRedBlue : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 3, G35::MAX_INTENSITY,
			 red_white_blue);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
color_t red_green(uint16_t sequence) {
  return sequence % 2 ? COLOR_RED : COLOR_GREEN;
}
class RedGreenChase : public LightProgram {
 public:
  void Init() {
    count_ = 1;
    sequence_ = 0;
  }
  void Do() {
    lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, red_green);
    if (count_ < LIGHT_COUNT) {
      ++count_;
    } else {
      ++sequence_;
    }
    delay(BULB_FRAME);
  }
 private:
  uint8_t count_;
  uint16_t sequence_;
};
//////////////////////////////////////////
class Meteorite : public LightProgram {
 public:
  void Init() {
    position_ = LAST_LIGHT + TAIL;
  }

  void Do() {
    if (position_ == LAST_LIGHT + TAIL) {
      position_ = 0;
      uint8_t r, g, b;
      r = rand() > (RAND_MAX / 2) ? 15 : 0;
      g = rand() > (RAND_MAX / 2) ? 15 : 0;
      b = rand() > (RAND_MAX / 2) ? 15 : 0;
      if (r == 0 && g == 0 && b == 0) {
        r = 15;
        g = 15;
        b = 15;
      }
      d_ = rand() % BULB_FRAME + 5;
      colors_[0] = COLOR(r, g, b);
      colors_[1] = COLOR(r * 3 / 4, g * 3 / 4, b * 3 / 4);
      colors_[2] = COLOR(r * 2 / 4, g * 2 / 4, b * 2 / 4);
      colors_[3] = COLOR(r * 1 / 4, g * 1 / 4, b * 1 / 4);
      colors_[4] = COLOR(r * 0 / 4, g * 0 / 4, b * 0 / 4);
    }

    for (int i = 0; i < TAIL; ++i) {
      int pos = position_ - i;
      lights.set_color_if_in_range(pos, G35::MAX_INTENSITY, colors_[i]);
    }
    ++position_;
    delay(d_);
  }
 private:
  static const uint8_t TAIL = 5;

  uint8_t d_;
  int16_t position_;
  color_t colors_[TAIL];
};
//////////////////////////////////////////
class Twinkle : public LightProgram {
 public:
  void Init() {
    lights.fill_random_max(0, LIGHT_COUNT, G35::MAX_INTENSITY);
  }

  void Do() {
    delay(BULB_FRAME);
    lights.set_color(rand() % LIGHT_COUNT, G35::MAX_INTENSITY,
		     G35::max_color(rand()));
  }
};
//////////////////////////////////////////
class Pulse : public LightProgram {
  public:
    void Init() {
      count_ = 1;
      sequence_ = 0;
      light_count_ = LIGHT_COUNT;
    }
    
    void Do() {
      lights.fill_sequence(0, count_, sequence_, 1, pulser);
      if (count_ < light_count_) {
        ++count_;
      } else {
        ++sequence_;
      }
    }
    
    static bool pulser(uint16_t sequence, color_t& color, uint8_t& intensity) {
      const int PHASE = 32;
      const int PULSE_RATE = 2;  // Above 1 is glitchy but IMO more attractive.
      color = G35::max_color((sequence + PHASE) / (PHASE * 2));
      intensity = abs(PHASE - (int)(sequence * PULSE_RATE) % (PHASE + PHASE));
      return true;
    }
    
  private:
    uint8_t count_;
    uint16_t sequence_;
    uint8_t light_count_;
};
//////////////////////////////////////////
class Orbit : public LightProgram {
 public:
  void Init() {
    should_erase_ = true;
    count_ = MAX_OBJECTS;
    light_count_ = LIGHT_COUNT;
    set_centers();
  }

  void Do() {
    delay(BULB_FRAME);
    for (int i = 0; i < count_; ++i) {
      Orbiter *o = &orbiter_[i];
      o->Do();
      uint8_t x = o->x_local(light_count_, orbiter_center_[i]);

      if (should_erase_ && last_x_[i] != x) {
        lights.set_color(last_x_[i], 255, COLOR_BLACK);
        last_x_[i] = x;
      }
      lights.set_color(x, 255, o->color());
    }
  }
  private:
    enum { MAX_OBJECTS = 10 };
    bool should_erase_;
    uint8_t count_;
    int16_t last_light_shifted_;
    int8_t light_count_;
    Orbiter orbiter_[MAX_OBJECTS];
    uint8_t orbiter_center_[MAX_OBJECTS];
    uint8_t last_x_[MAX_OBJECTS];
    
    void set_centers() {
      for (int i = 0; i < count_; ++i) {
        orbiter_center_[i] = rand() % light_count_;
        last_x_[i] = 0;
      }
    }
};
//////////////////////////////////////////
class Stereo : public LightProgram {
 public:
  void Init() {
    light_count_ = lights.get_light_count();
    half_light_count_ = (float)light_count_ / 2.0;
    level0_ = half_light_count_ * 0.5;
    level1_ = half_light_count_ * 0.1666;
    level2_ = half_light_count_ * 0.1666;
    level3_ = half_light_count_ * 0.1666;
    step_ = 0;
    peak_ = 0;
    lights.fill_color(0, light_count_, 255, COLOR_BLACK);
  }

  void Do() {
    delay(BULB_FRAME);
    float wave = level0_ + sin(step_) * level1_ + sin(step_ * .7) * level2_ + sin(step_ * .3) * level3_;
    if (wave > peak_) {
      peak_ = wave;
    } else {
      peak_ *= 0.99;
    }
    uint8_t i = wave;
    while (i--) {
      lights.set_color(i, 255, COLOR_GREEN);
      lights.set_color(light_count_ - i, 255, COLOR_GREEN);
    }
    uint8_t halfway = lights.get_halfway_point();
    uint8_t peak_i = peak_;
    for (i = wave; i < halfway; ++i) {
      uint8_t color = i == peak_i ? COLOR_RED : COLOR_BLACK;
      lights.set_color(i, 255, color);
      lights.set_color(light_count_ - i, 255, color);
    }
    step_ += 0.4;
  }
  private:
    uint8_t light_count_;
    float half_light_count_;
    float level0_, level1_, level2_, level3_;
    float step_, peak_;
};
//////////////////////////////////////////
class Inchworm : public LightProgram {
 public:
  void Init() {
    should_erase_ = true;
    count_ = MAX_OBJECTS;
    light_count_ = LIGHT_COUNT;
    set_centers();
  }

  void Do() {
    delay(BULB_FRAME);
    for (int i = 0; i < count_; ++i) {
      Orbiter *o = &orbiter_[i];
      o->Do();
      uint8_t x = o->x_local(light_count_, orbiter_center_[i]);

      if (should_erase_ && last_x_[i] != x) {
        lights.set_color(last_x_[i], 255, COLOR_BLACK);
        last_x_[i] = x;
      }
      lights.set_color(x, 255, o->color());
    }
  }
  private:
    enum { MAX_OBJECTS = 10 };
    bool should_erase_;
    uint8_t count_;
    int16_t last_light_shifted_;
    int8_t light_count_;
    Orbiter orbiter_[MAX_OBJECTS];
    uint8_t orbiter_center_[MAX_OBJECTS];
    uint8_t last_x_[MAX_OBJECTS];
    
    void set_centers() {
      for (int i = 0; i < count_; ++i) {
        orbiter_center_[i] = rand() % light_count_;
        last_x_[i] = 0;
      }
    }
};
//////////////////////////////////////////
class MEORainbow : public LightProgram {
  #define PATTERN_COUNT (8)
  public:
    void Init(uint8_t _pattern) {
      wait_ = 0;
      pattern_ = _pattern;
      step_ = 0;
      light_count_ = LIGHT_COUNT;
    }

    void Do() {
      delay(BULB_FRAME);
      bool fortyEight;
      for (int i=0; i < light_count_; i++) {
        switch (pattern_ % 8) {
        case 0:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::LineRG((i + step_) % 32));
          break;
        case 1:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                         MEORainbow::LineGB((i + step_) % 32));
          break;
        case 2:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::LineBR((i + step_) % 32));
          break;
        case 3:
          fortyEight = true;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::Wheel((i + step_) % 48));
          break;
        case 4:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::LineRG(((i * 32 / light_count_) + step_) % 32));
          break;
        case 5:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::LineGB(((i * 32 / light_count_) + step_) % 32));
          break;
        case 6:
          fortyEight = false;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::LineBR(((i * 32 / light_count_) + step_) % 32));
          break;
        case 7:
          fortyEight = true;
          lights.fill_color(i, 1, G35::MAX_INTENSITY,
                        MEORainbow::Wheel(((i * 48 / light_count_) + step_) % 48));
          break;
        }
      }

  // reset at end of wheel or line
    ++step_;
    if (((step_ == 48) && fortyEight) || ((step_ == 32) && !fortyEight)) {
      step_ = 0;
    }
    delay(wait_);
  }
  
    uint32_t Wheel(uint16_t WheelPos) {
      byte r, g, b;
      switch (WheelPos / 16) {
        case 0:
          r = 15 - WheelPos % 16; // red down
          g = WheelPos % 16;       // green up
          b = 0;                    // blue off
          break;
        case 1:
          g = 15 - WheelPos % 16; // green down
          b = WheelPos % 16;       // blue up
          r = 0;                    // red off
          break;
        case 2:
          b = 15 - WheelPos % 16; // blue down
          r = WheelPos % 16;       // red up
          g = 0;                    // green off
          break;
      }
      return (COLOR(r,g,b));
    }
    
    uint32_t LineRG(uint16_t WheelPos) {
      byte r, g, b;
      switch (WheelPos / 16) {
        case 0:
          r = 15 - WheelPos % 16; // red down
          g = WheelPos % 16;       // green up
          b = 0;                  // blue off
          break;
        case 1:
          r = WheelPos % 16;       // red up
          g = 15 - WheelPos % 16; // green down
          b = 0;                  // blue off
          break;
      }
      return (COLOR(r,g,b));
    }
    
    uint32_t LineGB(uint16_t WheelPos) {
      byte r, g, b;
      switch (WheelPos / 16) {
        case 0:
          r = 0;                    // red off
          g = 15 - WheelPos % 16; // green down
          b = WheelPos % 16;       // blue up
          break;
        case 1:
          r = 0;                    // red off
          g = WheelPos % 16;       // green up
          b = 15 - WheelPos % 16; // blue down
          break;
      }
      return (COLOR(r,g,b));
    }
    
    uint32_t LineBR(uint16_t WheelPos) {
      byte r, g, b;
      switch (WheelPos / 16) {
        case 0:
          r = WheelPos % 16;       // red up
          g = 0;                    // green off
          b = 15 - WheelPos % 16; // blue down
          break;
        case 1:
          r = 15 - WheelPos % 16; // red down
          g = 0;                    // green off
          b = WheelPos % 16;       // blue up
          break;
      }
      return (COLOR(r,g,b));
    }
  
  private:
    uint8_t light_count_;
    uint8_t wait_;
    uint8_t pattern_;
    uint8_t step_;
};
//////////////////////////////////////////
class MEOSineWave : public LightProgram {
  #define PI 3.14159265
  public:
  void Init(uint8_t _pattern) {
    light_count_ = LIGHT_COUNT;
    preFill_ = true;
    strobe_ = true;
    wait_ = 0;
    colorMain_ = COLOR(0,0,15);
    colorHi_ = COLOR(12,12,12);
    colorLo_ = COLOR(0,0,0);
    wavesPerString_ = 2;
    rainbowMain_ = true;
    step_ = 0;
    step2_ = 0;
    pattern_ = _pattern;
  }

  void Do() {
    delay(BULB_FRAME);
    switch (pattern_ % 7)
    {
    case 0:
        rainbowMain_ = false;
        colorMain_ = COLOR(15,0,0);
        break;
    case 1:
        rainbowMain_ = false;
        colorMain_ = COLOR(0,15,0);
        break;
    case 2:
        rainbowMain_ = false;
        colorMain_ = COLOR(0,0,15);
        break;
    case 3:
        rainbowMain_ = false;
        colorMain_ = COLOR(8,8,0);
        break;
    case 4:
        rainbowMain_ = false;
        colorMain_ = COLOR(8,0,8);
        break;
    case 5:
        rainbowMain_ = false;
        colorMain_ = COLOR(0,8,8);
        break;
    case 6:
        rainbowMain_ = true;
        break;
    }

    float y;
    byte  rMain, gMain, bMain, rOut, gOut, bOut, rhi, ghi, bhi, rlo, glo, blo, rRainbow, gRainbow, bRainbow;

    // Need to decompose colors into their r, g, b elements
    bMain = (colorMain_ >> 8) & 0xf;
    gMain = (colorMain_ >>  4) & 0xf;
    rMain =  colorMain_        & 0xf;

    bhi = (colorHi_ >> 8) & 0xf;
    ghi = (colorHi_ >>  4) & 0xf;
    rhi =  colorHi_        & 0xf;

    blo = (colorLo_ >> 8) & 0xf;
    glo = (colorLo_ >>  4) & 0xf;
    rlo =  colorLo_        & 0xf;

    uint32_t colorRainbow;
    colorRainbow = MEOSineWave::Wheel(step2_ % 48);

    bRainbow = (colorRainbow >> 8) & 0xf;
    gRainbow = (colorRainbow >>  4) & 0xf;
    rRainbow =  colorRainbow  & 0xf;

    for(int i = 0; i < light_count_; i++)
    {
        y = sin(PI * wavesPerString_ * (float)(step_ + i) / (float)light_count_);
        if(y >= 0.0)
        {
            // Peaks of sine wave are white
            y  = 1.0 - y; // Translate Y to 0.0 (top) to 1.0 (center)
            if (rainbowMain_)
            {
                rOut = rhi - (byte)((float)(rhi - rRainbow) * y);
                gOut = ghi - (byte)((float)(ghi - gRainbow) * y);
                bOut = bhi - (byte)((float)(bhi - bRainbow) * y);
            }
            else
            {
                rOut = rhi - (byte)((float)(rhi - rMain) * y);
                gOut = ghi - (byte)((float)(ghi - gMain) * y);
                bOut = bhi - (byte)((float)(bhi - bMain) * y);
            }
        }
        else
        {
            // Troughs of sine wave are black
            y += 1.0; // Translate Y to 0.0 (bottom) to 1.0 (center)
            if (rainbowMain_)
            {
                rOut = rlo + (byte)((float)(rRainbow) * y);
                gOut = glo + (byte)((float)(gRainbow) * y);
                bOut = blo + (byte)((float)(bRainbow) * y);
            }
            else
            {
                rOut = rlo + (byte)((float)(rMain) * y);
                gOut = glo + (byte)((float)(gMain) * y);
                bOut = blo + (byte)((float)(bMain) * y);
            }
        }
        lights.fill_color(i, 1, G35::MAX_INTENSITY, COLOR(rOut, gOut, bOut));
    }

    step_++;
    step2_++;
    if (step2_ == 48) // all 48 colors in the wheel
    {
        step2_ = 0;
    }

    delay(wait_);
  }
  
  uint32_t Wheel(uint16_t WheelPos)
{
    byte r, g, b;
    switch(WheelPos / 16)
    {
    case 0:
        r = 15 - WheelPos % 16; // red down
        g = WheelPos % 16;       // green up
        b = 0;                    // blue off
        break;
    case 1:
        g = 15 - WheelPos % 16; // green down
        b = WheelPos % 16;       // blue up
        r = 0;                    // red off
        break;
    case 2:
        b = 15 - WheelPos % 16; // blue down
        r = WheelPos % 16;       // red up
        g = 0;                    // green off
        break;
    }
    return(COLOR(r,g,b));
}
  
  private:
    uint8_t light_count_;
    bool preFill_;
    bool strobe_;
    uint8_t wait_;
    uint32_t colorMain_;
    uint32_t colorHi_;
    uint32_t colorLo_;
    uint8_t wavesPerString_;
    bool rainbowMain_;
    uint32_t step_;
    uint32_t step2_;
    uint8_t pattern_;
};
//////////////////////////////////////////
class MEOChasing : public LightProgram {
  public:
  void Init(uint8_t _pattern) {
    light_count_ = LIGHT_COUNT;
    count_ = 1;
    sequence_ = 0;
    wait_ = 10;
    pattern_ = _pattern;
  }
  

  void Do() {
    delay(BULB_FRAME);
     switch (pattern_ % 24)
    {
    case 0:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, GreenAccentedAlalogic);
        break;
    case 1:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, RedAccentedAlalogic);
        break;
    case 2:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, BlueAccentedAlalogic);
        break;
    case 3:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, PurplyBlue);
        break;
    case 4:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, Valentines);
        break;
    case 5:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, BlueTriad);
        break;
    case 6:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, BlueTetrad);
        break;
    case 7:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, PurpleTetrad);
        break;
    case 8:
        lights.fill_sequence(0, count_, sequence_, 1, G35::MAX_INTENSITY, GreenTetrad);
        break;
    case 9:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, RGBY);
        break;
    case 10:
        lights.fill_sequence(0, count_, sequence_, 10, G35::MAX_INTENSITY, RWB);
        break;
    case 11:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, RC);
        break;
    case 12:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, GM);
        break;
    case 13:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, BY);
        break;
    case 14:
        lights.fill_sequence(0, count_, sequence_, 2, G35::MAX_INTENSITY, RCGMBY);
        break;
    case 15:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, RG);
        break;
    case 16:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, GB);
        break;
    case 17:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, BR);
        break;
    case 18:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, CM);
        break;
    case 19:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, MY);
        break;
    case 20:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, YC);
        break;
    case 21:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, RGB);
        break;
    case 22:
        lights.fill_sequence(0, count_, sequence_, 5, G35::MAX_INTENSITY, CYM);
        break;
    case 23:
        lights.fill_sequence(0, count_, sequence_, 2, G35::MAX_INTENSITY, PastelRGB);
        break;
    }
    if (count_ < light_count_)
    {
        ++count_;
    }
    else
    {
        ++sequence_;
    }
    delay(wait_);
  }
  
static color_t PastelRGB(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR(0xf, 0x2, 0x1);
    }
    if (sequence == 1)
    {
        return COLOR(0x8, 0xf, 0x2);
    }
    return COLOR(0xb, 0x5, 0xf);
}

static color_t RGBY(uint16_t sequence)
{
    sequence = sequence % 4;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    if (sequence == 1)
    {
        return COLOR_GREEN;
    }
    if (sequence == 2)
    {
        return COLOR_BLUE;
    }
    return COLOR_YELLOW;
}

static color_t RWB(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    if (sequence == 1)
    {
        return COLOR_WHITE;
    }
    return COLOR_BLUE;
}

static color_t RC(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    return COLOR_CYAN;
}

static color_t GM(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_GREEN;
    }
    return COLOR_MAGENTA;
}

static color_t BY(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_BLUE;
    }
    return COLOR_YELLOW;
}

static color_t RG(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    return COLOR_GREEN;
}

static color_t GB(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_GREEN;
    }
    return COLOR_BLUE;
}

static color_t BR(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_BLUE;
    }
    return COLOR_RED;
}

static color_t CM(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_CYAN;
    }
    return COLOR_MAGENTA;
}

static color_t MY(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_MAGENTA;
    }
    return COLOR_YELLOW;
}

static color_t YC(uint16_t sequence)
{
    sequence = sequence % 2;
    if (sequence == 0)
    {
        return COLOR_YELLOW;
    }
    return COLOR_CYAN;
}

static color_t RGB(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    if (sequence == 1)
    {
        return COLOR_GREEN;
    }
    return COLOR_BLUE;
}

static color_t CYM(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR_CYAN;
    }
    if (sequence == 1)
    {
        return COLOR_YELLOW;
    }
    return COLOR_MAGENTA;
}

static color_t RCGMBY(uint16_t sequence)
{
    sequence = sequence % 6;
    if (sequence == 0)
    {
        return COLOR_RED;
    }
    if (sequence == 1)
    {
        return COLOR_CYAN;
    }
    if (sequence == 2)
    {
        return COLOR_GREEN;
    }
    if (sequence == 3)
    {
        return COLOR_MAGENTA;
    }
    if (sequence == 4)
    {
        return COLOR_BLUE;
    }
    return COLOR_YELLOW;
}

static color_t PurplyBlue(uint16_t sequence)
{
    sequence = sequence % 6;
    if (sequence == 0)
    {
        return COLOR(0,0,11);
    }
    if (sequence == 1)
    {
        return COLOR(3,0,10);
    }
    if (sequence == 2)
    {
        return COLOR(6,0,9);
    }
    if (sequence == 3)
    {
        return COLOR(8,0,8);
    }
    if (sequence == 4)
    {
        return COLOR(6,0,9);
    }
    return COLOR(3,0,10); //5
}

static color_t Valentines(uint16_t sequence)
{
    sequence = sequence % 10;
    if (sequence == 0)
    {
        return COLOR(11,0,0);
    }
    if (sequence == 1)
    {
        return COLOR(11,1,1);
    }
    if (sequence == 2)
    {
        return COLOR(10,2,2);
    }
    if (sequence == 3)
    {
        return COLOR(10,3,3);
    }
    if (sequence == 4)
    {
        return COLOR(9,4,4);
    }
    if (sequence == 5)
    {
        return COLOR(9,9,9);
    }
    if (sequence == 6)
    {
        return COLOR(9,4,4);
    }
    if (sequence == 7)
    {
        return COLOR(10,3,3);
    }
    if (sequence == 8)
    {
        return COLOR(10,2,2);
    }
    return COLOR(11,1,1); //5
}

static color_t BlueBronze(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR(0x0, 0x2, 0x5);
    }
    if (sequence == 1)
    {
        return COLOR(0x9, 0x8, 0x1);
    }
    return COLOR(0x9, 0x5, 0x1);
}


static color_t BlueTriad(uint16_t sequence)
{
    sequence = sequence % 6;
    if (sequence == 1)
    {
        return COLOR(0xF, 0x5, 0x0);
    }
    if (sequence == 5)
    {
        return COLOR(0xF, 0xC, 0x0);
    }
    return COLOR(0x0, 0x8, 0xF);
}

static color_t BlueTetrad(uint16_t sequence)
{
    sequence = sequence % 4;
    if (sequence == 0)
    {
        return COLOR(0x1, 0x4, 0xB);
    }
    if (sequence == 1)
    {
        return COLOR(0x4, 0x1, 0xB);
    }
    if (sequence == 2)
    {
        return COLOR(0x0, 0xA, 0xA);
    }
    return COLOR(0xF, 0xB, 0x0);
}

static color_t PurpleTetrad(uint16_t sequence)
{
    sequence = sequence % 4;
    if (sequence == 0)
    {
        return COLOR(0xF, 0x0, 0x9);
    }
    if (sequence == 1)
    {
        return COLOR(0xF, 0x0, 0x0);
    }
    if (sequence == 2)
    {
        return COLOR(0xA, 0x0, 0xF);
    }
    return COLOR(0xB, 0xF, 0x0);
}

static color_t GreenTetrad(uint16_t sequence)
{
    sequence = sequence % 4;
    if (sequence == 0)
    {
        return COLOR(0xB, 0xF, 0x0);
    }
    if (sequence == 1)
    {
        return COLOR(0x3, 0xF, 0x0);
    }
    if (sequence == 2)
    {
        return COLOR(0xF, 0xF, 0x0);
    }
    return COLOR(0xF, 0x0, 0xA);
}

static color_t RedAccentedAlalogic(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR(0xF, 0x0, 0x9);
    }
    if (sequence == 1)
    {
        return COLOR(0xF, 0x7, 0x0);
    }
    if (sequence == 2)
    {
        return COLOR(0xF, 0x0, 0x0);
    }
    return COLOR(0x0, 0xF, 0x0);
}

static color_t GreenAccentedAlalogic(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR(0xA, 0xF, 0x0);
    }
    if (sequence == 1)
    {
        return COLOR(0x0, 0xF, 0xF);
    }
    if (sequence == 2)
    {
        return COLOR(0x0, 0xF, 0x0);
    }
    return COLOR(0xF, 0x0, 0x0);
}

static color_t BlueAccentedAlalogic(uint16_t sequence)
{
    sequence = sequence % 3;
    if (sequence == 0)
    {
        return COLOR(0x0, 0x9, 0xF); //30 degrees anti-clockwise of Primary
    }
    if (sequence == 1)
    {
        return COLOR(0x7, 0x0, 0xF); //30 degrees clockwise of Primary
    }
    if (sequence == 2)   //Primary
    {
        return COLOR(0x0, 0x0, 0xF);
    }
    return COLOR(0xF, 0xC, 0x0); //180 degrees - Complimentary
}
   
  private:
    uint8_t light_count_;
    uint8_t count_;
    uint16_t sequence_;
    uint16_t wait_;
    uint8_t pattern_;
};
//////////////////////////////////////////
class TreeStatic : public LightProgram {
 public:
  void Init(uint8_t blink) {
    light_count_ = LIGHT_COUNT;
    blink_ = blink;
    i_ = 1;
    lights.fill_color(0,light_count_,G35::MAX_INTENSITY,COLOR_GREEN);
    lights.set_color(STAR_BULB,G35::MAX_INTENSITY,COLOR_WHITE);
    lights.set_color(4,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(6,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(10,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(13,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(16,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(18,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(21,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(26,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(29,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(33,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(37,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(39,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(43,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(46,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(49,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
  }

  void Do() {
    delay(BULB_FRAME*10);
    if (blink_){
      if (i_) {
        lights.set_color(STAR_BULB,G35::MAX_INTENSITY,COLOR_WHITE);
        i_ = 0;
        }
      else {
        lights.set_color(STAR_BULB,G35::MAX_INTENSITY,COLOR_BLACK);
        i_ = 1;
      }
    }      
  }
  private:
    uint8_t light_count_;
    uint8_t blink_;
    uint8_t i_;
};
//////////////////////////////////////////
class TreeTwinkle : public LightProgram {
 public:
  void Init(uint8_t rate) {
    light_count_ = LIGHT_COUNT;
    rate_ = rate;
    lights.fill_color(0,light_count_,G35::MAX_INTENSITY,COLOR_GREEN);
    lights.set_color(STAR_BULB,G35::MAX_INTENSITY,COLOR_WHITE);
  }

  void Do() {
    delay(rate_);
    lights.set_color(4,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(6,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(10,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(13,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(16,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(18,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(21,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(26,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(29,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(33,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(37,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(39,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(43,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(46,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
    lights.set_color(49,random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
  }
  private:
    uint8_t light_count_;
    uint8_t rate_;
};
//////////////////////////////////////////
//Stock-type Programs
SteadyWhite steady_white;
SteadyMulti steady_multi;
CrossOverWave cross_over_wave;
ForwardWave forward_wave;
ChasingRainbow chasing_rainbow;
DoubleRainbow double_rainbow;
AlternateDirectionalWave alternate_directional_wave;
FadeInFadeOutSolidColors fade_in_fade_out_solid_colors;
BidirectionalWave bidirectional_wave;
ChasingSolidColors chasing_solid_colors;
FadeInFadeOutMultiColors fade_in_fade_out_multi_colors;
ChasingTwoColors chasing_two_colors;
RandomSparkling random_sparkling;
ChasingMultiColors chasing_multi_colors;
ChasingWhiteRedBlue chasing_white_red_blue;
ChasingRedWhiteGreen chasing_red_white_green;

// Sowbug Programs
Meteorite meteorite;
Twinkle twinkle;
RedGreenChase red_green_chase;
Pulse pulse;
Orbit orbit;
Stereo stereo;
Inchworm inchworm;

//MEO Programs
MEORainbow MEO_rainbow;
MEOSineWave MEO_sine_wave;
MEOChasing MEO_chasing;

//Sculpture
TreeStatic tree_static;
TreeTwinkle tree_twinkle;

LightProgram* programs[] = {
  &steady_white, //0
  &steady_multi, //1
  &cross_over_wave, //2
  &forward_wave, //3
  &chasing_rainbow, //4
  &double_rainbow, //5
  &alternate_directional_wave, //6
  &fade_in_fade_out_solid_colors, //7
  &bidirectional_wave, //8
  &chasing_solid_colors, //9
  &fade_in_fade_out_multi_colors, //10
  &chasing_two_colors, //11
  &random_sparkling, //12
  &chasing_multi_colors, //13
  &chasing_white_red_blue, //14
  &chasing_red_white_green, //15
  &meteorite, //16
  &twinkle, //17
  &red_green_chase, //18
  &pulse, //19
  &orbit, //20
  &stereo, //21
  &inchworm, //22
  &MEO_rainbow,//23
  &MEO_sine_wave,//24
  &MEO_chasing,//25
  &tree_static,//26
  &tree_twinkle//27
};
#define PROGRAM_COUNT (sizeof(programs) / sizeof (LightProgram*))

//*********************************************************************
//* Main Loop

void loop() {
  static LightProgram *program;
  //Handle Serial Commands
  if (Serial.available())
    handleInput(Serial.read());
    
  //Handle Wireless Packets
  if (rf12_recvDone() && rf12_crc == 0 && rf12_len == sizeof (payload) ) {
    command = *(payload*) rf12_data;
    
    if ((command.dest >= BROADCAST_RANGE) && ACK_NODE){ //Only ack on broadcasts if ACK_NODE is 1
      rf12_sendStart(RF12_HDR_CTL | RF12_HDR_DST | (CONTROL_NODE & RF12_HDR_MASK),0,0);
      rf12_sendWait(0);
    }
    
    if ((int)command.dest == NODE_NUM){ //Always ack if directly addressed
      rf12_sendStart(RF12_HDR_CTL | RF12_HDR_DST | (CONTROL_NODE & RF12_HDR_MASK),0,0);
      rf12_sendWait(0);
    }
    
    if ((command.dest >= BROADCAST_RANGE) || ((int)command.dest == NODE_NUM)){ //Use command if sent to my node or the broadcast address
      if (!((myCommand.dest==command.dest)&&(myCommand.mode==command.mode)&&(myCommand.start_bulb==command.start_bulb)&&(myCommand.end_bulb==command.end_bulb)&&(myCommand.bulb_color==command.bulb_color)&&
      (myCommand.bulb_intensity==command.bulb_intensity)&&(myCommand.option==command.option)&&(myCommand.duration==command.duration))){
        myCommand = *(payload*) rf12_data; //New payload data
        lightSet = 0;
        durations = myCommand.duration;
        time = now();
        if(myCommand.mode > 0) 
          lightShow = 1;
        else if (myCommand.mode == 0)
          lightShow = 0;
      } 
    }
  }
  
  //Do Light Output
  if (lightShow) {
    switch (myCommand.mode) {
      default:
        break;
      case 0: //Off
        lights.fill_color(0, LIGHT_COUNT,0,COLOR_BLACK);
        lightSet = 0;
        break;
      case 1: //Program
        if (myCommand.duration == 0) {
          program = programs[myCommand.option];
          if (lightSet == 0) {
            if (myCommand.option > 22)
              program->Init(myCommand.bulb_intensity);
            else
              program->Init();
            lightSet = 1;
          }
          program->Do();
        }
        if (myCommand.duration > 0) {
          if (now() <= time + durations) {
            program = programs[myCommand.option];
            if (lightSet == 0) {
              if (myCommand.option > 22)
                program->Init(myCommand.bulb_intensity);
              else
                program->Init();
              lightSet = 1;
            }
            program->Do();
          } else lightShow = 0;
        }
        break;
      case 2: //Fill
        if (lightSet == 0) {  
          if (myCommand.start_bulb == myCommand.end_bulb)
            lights.set_color(myCommand.start_bulb,myCommand.bulb_intensity,myCommand.bulb_color);
          else
            lights.fill_color(myCommand.start_bulb,myCommand.end_bulb - myCommand.start_bulb + 1, myCommand.bulb_intensity, myCommand.bulb_color);
          if (myCommand.option == 1) { //strobe
            delay(myCommand.duration);
            if (myCommand.start_bulb == myCommand.end_bulb)
              lights.set_color(myCommand.start_bulb,0,COLOR_BLACK);
            else
              lights.fill_color(myCommand.start_bulb,myCommand.end_bulb - myCommand.start_bulb + 1, 0, COLOR_BLACK);
            delay(myCommand.duration);
          } else lightSet = 1;
        }
        break;
      case 3: //Fade
        switch (myCommand.option) {
          default:
            break;
          case 0: //In from black
            if (lightSet == 0) {
              lights.fill_color(myCommand.start_bulb, myCommand.end_bulb - myCommand.start_bulb + 1, 0, myCommand.bulb_color);
              lightSet = 1;
              lights.fade_in(myCommand.duration);
            }
            break;
          case 1: //Out to black
            if (lightSet == 0) {
            lights.fade_out(myCommand.duration);
            lightSet = 1;
            }
            break;
        }
        break;
      case 4: //Chase
        switch (myCommand.option) {
          default:
            break;
          case 0: //Up
            if (lightSet == 0) {
              for(int i=myCommand.start_bulb;i<=myCommand.end_bulb+3;i++) {
                if (i<myCommand.end_bulb+1)
                  lights.set_color(i,myCommand.bulb_intensity,myCommand.bulb_color);
                if (i>0 && i<myCommand.end_bulb+2)
                  lights.set_color(i-1,myCommand.bulb_intensity/2,myCommand.bulb_color);
                if (i>1 && i<myCommand.end_bulb+3)
                  lights.set_color(i-2,myCommand.bulb_intensity/4,myCommand.bulb_color);
                if (i>myCommand.start_bulb+2)
                  lights.set_color(i-3,0,COLOR_BLACK);
                delay(myCommand.duration);
              }
              lightSet = 1;
            }
            break;
          case 1: //Down
            if (lightSet == 0) {
              for(int i=myCommand.end_bulb+3;i>=myCommand.start_bulb;i--) {
                if (i>=myCommand.start_bulb+3)
                  lights.set_color(i-3,myCommand.bulb_intensity,myCommand.bulb_color);
                if (i<=myCommand.end_bulb+2 && i>=myCommand.start_bulb+2)
                  lights.set_color(i-2,myCommand.bulb_intensity/2,myCommand.bulb_color);
                if (i<=myCommand.end_bulb+1 && i>=myCommand.start_bulb+1)
                  lights.set_color(i-1,myCommand.bulb_intensity/4,myCommand.bulb_color);
                if (i<=myCommand.end_bulb)
                  lights.set_color(i,0,COLOR_BLACK);
                delay(myCommand.duration);
              }
              lightSet = 1;
            }
            break;
          case 2: //Fill in
            if (lightSet == 0) {
              for(int i=myCommand.start_bulb;i<=myCommand.end_bulb;i++) {
                lights.set_color(i,myCommand.bulb_intensity,myCommand.bulb_color);
                delay(myCommand.duration);
              }
              lightSet = 1;
            }
            break;
          case 3: //Wipe out
            if (lightSet == 0) {
              for(int i=myCommand.end_bulb;i>=myCommand.start_bulb;i--) {
                lights.set_color(i,0,COLOR_BLACK);
                delay(myCommand.duration);
              }
              lightSet = 1;
            }
            break;
        }          
        break;
      case 5: //Random
        lights.fill_color(0, LIGHT_COUNT,0,COLOR_BLACK);
        for (int i=0; i<myCommand.option; i++) {
          if (myCommand.bulb_color == 0) { //Random color
            if (myCommand.bulb_intensity == 0) //Random brightness
              lights.set_color(random(0, LIGHT_COUNT), random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), lights.color_hue(random(0, 100)%(HUE_MAX+1)));
            else
              lights.set_color(random(0, LIGHT_COUNT), myCommand.bulb_intensity, lights.color_hue(random(0, 100)%(HUE_MAX+1)));
          }
          else {
            if (myCommand.bulb_intensity == 0)
              lights.set_color(random(0, LIGHT_COUNT), random(0, G35::MAX_INTENSITY)%(G35::MAX_INTENSITY+1), myCommand.bulb_color);
            else
              lights.set_color(random(0, LIGHT_COUNT), myCommand.bulb_intensity, myCommand.bulb_color);
          }
        }
        delay(myCommand.duration);
        break;
      case 6: //Dissolve
        for (int i=0; i<myCommand.option; i++) {
          lights.set_color(random(0, LIGHT_COUNT),0, COLOR_BLACK);
        }
        delay(myCommand.duration);
        break;
    }
  }
  else
    lights.fill_color(0, LIGHT_COUNT,0,COLOR_BLACK);
}
//*********************************************************************
