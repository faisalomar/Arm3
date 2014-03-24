//------------------------------------------------------------------------------
// Arm3 - Three Axis Robot Arm based on code from 6 Axis CNC Demo v2
// dan@marginallycelver.com 2014-03-23
//------------------------------------------------------------------------------
// Copyright at end of file.
// please see http://www.marginallyclever.com/ for more information.


//------------------------------------------------------------------------------
// CONSTANTS
//------------------------------------------------------------------------------
//#define VERBOSE              (1)  // add to get a lot more serial output.
//#define DEBUG              (1)  // add to get a lot more serial output.

#define VERSION              (2)  // firmware version
#define BAUD                 (57600)  // How fast is the Arduino talking?
#define MAX_BUF              (64)  // What is the longest message Arduino can store?
#define MIN_STEP_DELAY       (1)
#define MAX_FEEDRATE         (1000000/MIN_STEP_DELAY)
#define MIN_FEEDRATE         (0.01)
#define NUM_AXIES            (3)
#define CM_PER_SEGMENT       (1)
#define DEFAULT_FEEDRATE     (500)

#define STEP_MULTIPLE        (16)  // microstepping
#define STEPS_PER_TURN       (400*STEP_MULTIPLE)  // 400 step per turn * microstepping

// machine dimensions
#define BASE_TO_SHOULDER_X   (5.37)  // measured in solidworks
#define BASE_TO_SHOULDER_Z   (9.55)  // measured in solidworks
#define SHOULDER_TO_ELBOW    (25)
#define ELBOW_TO_WRIST       (25)
#define WRIST_TO_FINGER      (4)

#define HOME_X               (13.3)
#define HOME_Y               (0)
#define HOME_Z               (23.91)

#define MAX_TOOLS            (6)

#define MAX_MOTORS           (6)

//------------------------------------------------------------------------------
// INCLUDES
//------------------------------------------------------------------------------
#include "Vector3.h"


//------------------------------------------------------------------------------
// STRUCTS
//------------------------------------------------------------------------------
// for line()
typedef struct {
  long delta;
  long absdelta;
  int dir;
  long over;
} Axis;



typedef struct {
  int motor_step_pin;
  int motor_dir_pin;
  int motor_enable_pin;
  int motor_scale;  // 1 or -1

  char limit_switch_pin;
  int limit_switch_state;
} Motor;


//------------------------------------------------------------------------------
// GLOBALS
//------------------------------------------------------------------------------
Axis a[4];  // for line()
Axis atemp;  // for line()

Motor motors[MAX_MOTORS];

char buffer[MAX_BUF];  // where we store the message until we get a ';'
int sofar;  // how much is in the buffer

float px, py, pz;  // location
float ox, oy, oz;

// speeds
float fr=0;  // human version
long step_delay;  // machine version

// settings
char mode_abs=1;  // absolute mode?


Vector3 tool_offset[MAX_TOOLS];
int current_tool=0;

//------------------------------------------------------------------------------
// METHODS
//------------------------------------------------------------------------------


/**
 * delay for the appropriate number of microseconds
 * @input ms how many milliseconds to wait
 */
void pause(long ms) {
  delay(ms/1000);
  delayMicroseconds(ms%1000);  // delayMicroseconds doesn't work for values > ~16k.
}


/**
 * Set the feedrate (speed motors will move)
 * @input nfr the new speed in steps/second
 */
void feedrate(float nfr) {
  if(fr==nfr) return;  // same as last time?  quit now.

  if(nfr>MAX_FEEDRATE || nfr<MIN_FEEDRATE) {  // don't allow crazy feed rates
    Serial.print(F("New feedrate must be greater than "));
    Serial.print(MIN_FEEDRATE);
    Serial.print(F("steps/s and less than "));
    Serial.print(MAX_FEEDRATE);
    Serial.println(F("steps/s."));
    return;
  }
  step_delay = 1000000.0/nfr;
  fr=nfr;
}


/**
 * Set the logical position
 * @input npx new position x
 * @input npy new position y
 */
void position(float npx,float npy,float npz) {
  // here is a good place to add sanity tests
  ox=npx;
  oy=npy;
  oz=npz;
}


/**
 * Supports movement with both styles of Motor Shield
 * @input newx the destination x position
 * @input newy the destination y position
 **/
void onestep(int motor_id,int dir) {
#ifdef VERBOSE
  char *letter="XYZE";
  Serial.print(letter[motor]);
#endif
  Motor &a = motors[motor_id];
  dir *= a.motor_scale;
  digitalWrite(a.motor_dir_pin,dir>0?LOW:HIGH);
  digitalWrite(a.motor_step_pin,HIGH);
  digitalWrite(a.motor_step_pin,LOW);
}


void motor_setup() {
  // set up the pins
  motors[0].motor_step_pin=17;
  motors[0].motor_dir_pin=16;
  motors[0].motor_enable_pin=48;
  motors[0].limit_switch_pin=37;

  motors[1].motor_step_pin=54;
  motors[1].motor_dir_pin=47;
  motors[1].motor_enable_pin=55;
  motors[1].limit_switch_pin=36;

  motors[2].motor_step_pin=57;
  motors[2].motor_dir_pin=56;
  motors[2].motor_enable_pin=62;
  motors[2].limit_switch_pin=35;

  motors[3].motor_step_pin=23;
  motors[3].motor_dir_pin=22;
  motors[3].motor_enable_pin=27;
  motors[3].limit_switch_pin=34;

  motors[4].motor_step_pin=26;
  motors[4].motor_dir_pin=25;
  motors[4].motor_enable_pin=24;
  motors[4].limit_switch_pin=33;

  motors[5].motor_step_pin=29;
  motors[5].motor_dir_pin=28;
  motors[5].motor_enable_pin=39;
  motors[5].limit_switch_pin=32;
  
  for(int i=0;i<MAX_MOTORS;++i) {  
    // set the motor pin & scale
    motors[i].motor_scale=((i%2)? -1:1);
    pinMode(motors[i].motor_step_pin,OUTPUT);
    pinMode(motors[i].motor_dir_pin,OUTPUT);
    pinMode(motors[i].motor_enable_pin,OUTPUT);
    // set the switch pin
    motors[i].limit_switch_state=HIGH;
    pinMode(motors[i].limit_switch_pin,INPUT);
    digitalWrite(motors[i].limit_switch_pin,HIGH);
  }
  
}


/**
 * Grips the power on the motors
 **/
void motor_enable() {
  int i;
  for(i=0;i<NUM_AXIES;++i) {
    digitalWrite(motors[i].motor_enable_pin,LOW);
  }
}


/**
 * Releases the power on the motors
 **/
void motor_disable() {
  int i;
  for(i=0;i<NUM_AXIES;++i) {
    digitalWrite(motors[i].motor_enable_pin,HIGH);
  }
}



/**
 * Convert cartesian XYZ to robot motor steps.
 * @return 0 if successful, 1 if the IK solution cannot be found.
 */
int IK(float x, float y,float z,float &a,float &b,float &c) {
  c = atan2(y,x) * STEPS_PER_TURN;
  
  // if we know the position of the wrist relative to the shoulder
  // we can use intersection of circles to find the elbow.
  // once we know the elbow position we can find the angle of each joint.
  // each angle can be converted to motor steps.
    
    // use intersection of circles to find two possible elbow points.
    // the two circles are the bicep (shoulder-elbow) and the forearm (elbow-wrist)
    // the distance between circle centers is d  
    Vector3 arm_plane(x,y,0);
    arm_plane.Normalize();

    // the finger (attachment point for the tool) is a short distance in "front" of the wrist joint
    Vector3 wrist(x,y,z);
    wrist -= arm_plane * WRIST_TO_FINGER;

    Vector3 shoulder = arm_plane;
    shoulder *= BASE_TO_SHOULDER_X;
    shoulder.z = BASE_TO_SHOULDER_Z;
    
    Vector3 es = wrist - shoulder;
    
    float d = es.Length();
    
    //a = (r0r0 - r1r1 + d*d ) / (2 d) 
    float r1=ELBOW_TO_WRIST;  // circle 1 centers on wrist
    float r0=SHOULDER_TO_ELBOW;  // circle 0 centers on shoulder
    if( d > ELBOW_TO_WRIST + SHOULDER_TO_ELBOW ) {
      // The points are impossibly far apart, no solution can be found.
      return 1;
    }
    
    a = ( r0 * r0 - r1 * r1 + d*d ) / ( 2.0*d );
    // find the midpoint
    Vector3 mid = es * ( a / d ) + shoulder;
    // with a and r0 we can find h, the distance from midpoint to the intersections.
    float h=sqrt(r0*r0-a*a);
    // the distance h on a line orthogonal to n and plane_normal gives us the two intersections.
    Vector3 o(arm_plane.y,-arm_plane.x,0);
    o.Normalize();
    Vector3 elbow = mid - o * h;
    //Vector3 elbow = mid + o * h;
    
  // find the shoulder angle using atan3(elbow-shoulder)
  float ax=elbow | arm_plane;
  float ay=elbow.z;
  b = atan2(ay,ax) * STEPS_PER_TURN;

  // find the elbow angle
  Vector3 temp = elbow - wrist;
  float bx = temp | arm_plane;
  float by = temp.z;
  a = atan2(by,bx) * -STEPS_PER_TURN;
  
#ifdef DEBUG
  Serial.print(F("IK="));
  Serial.print(a);
  Serial.print("\t");
  Serial.print(b);
  Serial.print("\t");
  Serial.print(c);
  Serial.print("\n");
#endif

  return 0;
}


/**
 * Uses bresenham's line algorithm to move both motors
 * @input newx the destination x position
 * @input newy the destination y position
 **/
void line(float newx,float newy,float newz) {
  a[0].delta = newx-px;
  a[1].delta = newy-py;
  a[2].delta = newz-pz;
  
  long i,j,maxsteps=0;

  for(i=0;i<NUM_AXIES;++i) {
    a[i].absdelta = abs(a[i].delta);
    a[i].dir = a[i].delta > 0 ? 1:-1;
    if( maxsteps < a[i].absdelta ) maxsteps = a[i].absdelta;
    a[i].over=0;
  }
  

#ifdef VERBOSE
  Serial.println(F("Start >"));
#endif
  
  for( i=0; i<maxsteps; ++i ) {
    for(j=0;j<NUM_AXIES;++j) {
      a[j].over += a[j].absdelta;
      if(a[j].over >= maxsteps) {
        a[j].over -= maxsteps;
        onestep(j,a[j].dir);
      }
    }
    pause(step_delay);
  }

  px=newx;
  py=newy;
  pz=newz;

#ifdef VERBOSE
  Serial.println(F("< Done."));
#endif
}


void arm_line(float x,float y,float z) {
  float a,b,c;
  IK(x,y,z,a,b,c);
  position(x,y,z);
  line(a,b,c);
}


void line_safe(float x,float y,float z) {
  // split up long lines to make them straighter?
  float dx=x-ox;
  float dy=y-oy;
  float dz=z-oz;
/*
  Serial.print("dx ");  Serial.println(dx);
  Serial.print("dy ");  Serial.println(dy);
  Serial.print("dz ");  Serial.println(dz);

  Serial.print("posx ");  Serial.println(posx);
  Serial.print("posy ");  Serial.println(posy);
  Serial.print("posz ");  Serial.println(posz);
*/
  float len=sqrt(dx*dx+dy*dy);
//  Serial.print("LEN ");  Serial.println(len);
  
  long pieces=floor(len/CM_PER_SEGMENT);
//  Serial.print("pieces ");  Serial.println(pieces);
  
  float x0=ox;
  float y0=oy;
  float z0=oz;
  float a;
  for(long j=0;j<pieces;++j) {
    a=(float)j/(float)pieces;
//  Serial.print("a ");  Serial.println(a);

    arm_line(dx*a+x0,
             dy*a+y0,
             dz*a+z0);
  }
  arm_line(x,y,z);
}


/**
 * Look for character /code/ in the buffer and read the float that immediately follows it.
 * @return the value found.  If nothing is found, /val/ is returned.
 * @input code the character to look for.
 * @input val the return value if /code/ is not found.
 **/
float parsenumber(char code,float val) {
  char *ptr=buffer;
  while(ptr && *ptr && ptr<buffer+sofar) {
    if(*ptr==code) {
      return atof(ptr+1);
    }
    ptr=strchr(ptr,' ')+1;
  }
  return val;
} 


void arm_tool_offset(int axis,float x,float y,float z) {
  tool_offset[axis].x=x;
  tool_offset[axis].y=y;
  tool_offset[axis].z=z;
}


Vector3 arm_get_end_plus_offset() {
  return Vector3(tool_offset[current_tool].x + ox,
                 tool_offset[current_tool].y + oy,
                 tool_offset[current_tool].z + oz);
}


void arm_tool_change(int tool_id) {
  if(tool_id < 0) tool_id=0;
  if(tool_id > MAX_TOOLS) tool_id=MAX_TOOLS;
  current_tool=tool_id;
}


void find_home() {
  // not implemented yet.
  // back up until each switch is touched.

  // knowing where the switches are we can now calculate the starting position of the robot.  
  position(HOME_X,HOME_Y,HOME_Z);
  IK(ox,oy,oz,px,py,pz);
}


/**
 * write a string followed by a float to the serial line.  Convenient for debugging.
 * @input code the string.
 * @input val the float.
 */
void output(char *code,float val) {
  Serial.print(code);
  Serial.println(val);
}


/**
 * print the current position, feedrate, and absolute mode.
 */
void where() {
  output("X",ox);
  output("Y",oy);
  output("Z",oz);
  output("F",fr);
  Serial.println(mode_abs?"ABS":"REL");
} 


/**
 * display helpful information
 */
void help() {
  Serial.print(F("GcodeCNCDemo4AxisV2 "));
  Serial.println(VERSION);
  Serial.println(F("Commands:"));
  Serial.println(F("M18; - disable motors"));
  Serial.println(F("M100; - this help message"));
  Serial.println(F("M114; - report position and feedrate"));
  Serial.println(F("F, G00, G01, G04, G17, G18, G28, G54-G59, G90-G92, M06 as described by http://en.wikipedia.org/wiki/G-code"));
}


/**
 * Read the input buffer and find any recognized commands.  One G or M command per line.
 */
void processCommand() {
  int cmd = parsenumber('G',-1);
  switch(cmd) {
  case  0: 
  case  1: { // move linear
    feedrate(parsenumber('F',fr));
    Vector3 offset=arm_get_end_plus_offset();
    line_safe( parsenumber('X',(mode_abs?offset.x:0)) + (mode_abs?0:offset.x),
               parsenumber('Y',(mode_abs?offset.y:0)) + (mode_abs?0:offset.y),
               parsenumber('Z',(mode_abs?offset.z:0)) + (mode_abs?0:offset.z) );
    break;
  }
  case  4:  pause(parsenumber('P',0)*1000);  break;  // dwell
  case 28:  find_home();  break;
  case 54:
  case 55:
  case 56:
  case 57:
  case 58:
  case 59: {  // 54-59 tool offsets
    int tool_id=cmd-54;
    arm_tool_offset(tool_id,parsenumber('X',tool_offset[tool_id].x),
                            parsenumber('Y',tool_offset[tool_id].y),
                            parsenumber('Z',tool_offset[tool_id].z));
    break;
  }
  case 90:  mode_abs=1;  break;  // absolute mode
  case 91:  mode_abs=0;  break;  // relative mode
  case 92:  { // set logical position
    Vector3 offset = arm_get_end_plus_offset();
    position( parsenumber('X',offset.x),
              parsenumber('Y',offset.y),
              parsenumber('Z',offset.z) );
    break;
  }
  default:  break;
  }

  cmd = parsenumber('M',-1);
  switch(cmd) {
  case 6:  arm_tool_change(parsenumber('T',0));  break;
  case 17:  motor_enable();  break;
  case 18:  motor_disable();  break;
  case 100:  help();  break;
  case 114:  where();  break;
  default:  break;
  }
}


/**
 * prepares the input buffer to receive a new message and tells the serial connected device it is ready for more.
 */
void ready() {
  sofar=0;  // clear input buffer
  Serial.print(F(">"));  // signal ready to receive input
}


void setup_tools() {
  for(int i=0;i<MAX_TOOLS;++i) {
    tool_offset[i].Set(0,0,0);
  }
}


/**
 * First thing this machine does on startup.  Runs only once.
 */
void setup() {
  Serial.begin(BAUD);  // open coms

  motor_setup();
  setup_tools();
  
  help();  // say hello
  feedrate(DEFAULT_FEEDRATE);  // set default speed
  find_home();
  ready();
}


/**
 * After setup() this machine will repeat loop() forever.
 */
void loop() {
  // listen for serial commands
  while(Serial.available() > 0) {  // if something is available
    char c=Serial.read();  // get it
    Serial.print(c);  // repeat it back so I know you got the message
    if(sofar<MAX_BUF) buffer[sofar++]=c;  // store it
    if(buffer[sofar-1]==';') break;  // entire message received
  }

  if(sofar>0 && buffer[sofar-1]==';') {
    // we got a message and it ends with a semicolon
    buffer[sofar]=0;  // end the buffer so string functions work right
    Serial.print(F("\r\n"));  // echo a return character for humans
    processCommand();  // do something with the command
    ready();
  }
}


/**
* This file is part of Arm3.
*
* Arm3 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Arm3 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Foobar. If not, see <http://www.gnu.org/licenses/>.
*/
