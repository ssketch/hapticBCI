
#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include <cstdlib>
#include <conio.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <string>
#include "chai3d.h"
#include "data.h"
#include "shared_Data.h"

void linkSharedDataToExperiment(shared_data& sharedData);
void initExperiment(void);
void updateExperiment(void);
void closeExperiment(void);

#endif  // EXPERIMENT_H
