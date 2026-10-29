#ifndef MS_TOUCH_H
#define MS_TOUCH_H
#define SCE_TOUCH_PORT_FRONT 0
#define SCE_TOUCH_PORT_BACK 1
#define SCE_TOUCH_SAMPLING_STATE_START 1
typedef struct SceTouchData { long long timeStamp; unsigned char fill[6]; unsigned int reportNum; struct { unsigned char id; unsigned short x,y; } report[8]; } SceTouchData;
int sceTouchSetSamplingState(int,int);
int sceTouchPeek(int,SceTouchData*,int);
int sceTouchRead(int,SceTouchData*,int);
#endif
typedef struct SceTouchPanelInfo { int infoA,infoB,infoC; int minAaX,minAaY,maxAaX,maxAaY; int minDispX,minDispY,maxDispX,maxDispY; int minRotX,minRotY,maxRotX,maxRotY; int dispX,dispY,rotX,rotY; } SceTouchPanelInfo;
int sceTouchGetPanelInfo(int,SceTouchPanelInfo*);
