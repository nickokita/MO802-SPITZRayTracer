#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include "RayTracer.h"

using namespace std;

struct parameters {
    int maxReflections;
    int superSamples;
    int depthComplexity;

    parameters(int argc, const char *argv[]) {
        maxReflections = 10;
        superSamples = atoi(argv[2]);
        depthComplexity = atoi(argv[3]);
    }
};
