
#ifndef NEUROTOUCH_H
#define NEUROTOUCH_H

#include <conio.h>
#include <cmath>
#include "cNeuroTouch.h"
#include "BCI.h"
#include "OSC_Listener.h"
#include "cForceSensor.h"
#include "cATIForceSensor.h"
#include "shared_Data.h"

void initNeuroTouch(void);
void linkSharedDataToNeuroTouch(shared_data& sharedData);
void updateNeuroTouch(void);
void updateCursor(void);
void computeForce(void);
void closeNeuroTouch(void);

#endif  // NEUROTOUCH_H
