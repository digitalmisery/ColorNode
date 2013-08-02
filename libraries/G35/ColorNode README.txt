I altered G35.cpp in the following ways:

#include "digitalWriteFast.h" - this .h file must also be present in order for the code to compile

hardcoded ColorNode output pin 19 in:
pinModeFast();
digtalWriteFast();

Changed delayMicroseconds(); timings to be exact per GE string protocol, which is possible with the use of digitalWriteFast since it uses direct port acesses.

Added steady_multi() to match other standard multi-color LED strings

