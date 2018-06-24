/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Caian Benedicto <caian@ggaunicamp.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to 
 * deal in the Software without restriction, including without limitation the 
 * rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 */


// This define enables the C++ wrapping code around the C API, it must be used
// only once per C++ module.
#define SPITZ_ENTRY_POINT

// Spitz serial debug enables a Spitz-compliant main function to allow 
// the module to run as an executable for testing purposes.
// #define SPITZ_SERIAL_DEBUG

#include <spitz/spitz.hpp>

#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <sys/time.h>

#include "RayTracer.h"
#include "Vector.h"
#include "Camera.h"
#include "Image.h"

// This class creates tasks.
class job_manager : public spitz::job_manager
{
private:
    int maxWidth;
    int curWidth;

    struct timeval time_start;

public:
    job_manager(int argc, const char *argv[], spitz::istream& jobinfo)
    {
        gettimeofday(&time_start, NULL);
        curWidth = 0;
        maxWidth = 1920;

        std::cout << "[JM] Job manager created." << std::endl;
    }
    
    bool next_task(const spitz::pusher& task)
    {
        spitz::ostream o;

        if (curWidth < maxWidth) {
            o << curWidth;
            int finalWidth = (curWidth + 15 > 1920) ? 1920 : curWidth + 15;

            o << finalWidth;
            std::cout << "[JM] Task for columns " << curWidth << " to " << finalWidth << " generated." << std::endl;

            curWidth = finalWidth;

            // Send the task to the Spitz runtime
            task.push(o);
            return true;
        }

        return false;
    }
    
    ~job_manager()
    {
        struct timeval time_end;
        gettimeofday(&time_end, NULL);

        float total = time_end.tv_sec - time_start.tv_sec;
        total += (time_end.tv_usec - time_start.tv_usec)/1000000.0f;

        std::cout << "[JM] Finished job in: " << total << " seconds" << std::endl;
    }
};

// This class executes tasks, preferably without modifying its internal state
// because this can lead to a break of idempotence between tasks. The 'run'
// method will not impose a 'const' behavior to allow libraries that rely 
// on changing its internal state, for instance, OpenCL (see clpi example).
class worker : public spitz::worker
{    
private:
    int maxReflections;
    int superSamples;
    int depthComplexity;

    RayTracer *rayTracer;
    
public:
    worker(int argc, const char *argv[])
    {
        maxReflections = 10;
        superSamples = atoi(argv[2]);
        depthComplexity = atoi(argv[3]);

        rayTracer = new RayTracer(1920, 1080, maxReflections, superSamples, depthComplexity);

        if (strcmp(argv[1], "-") == 0) {
            (*rayTracer).readScene(std::cin);
        } else {
            const char *inFile = argv[1];
            std::ifstream inFileStream;
            inFileStream.open(inFile, std::ifstream::in);

            if (inFileStream.fail()) {
                std::cerr << "Failed opening file" << std::endl;
                exit(EXIT_FAILURE);
            }
            (*rayTracer).readScene(inFileStream);
        }

        std::cout << "[WK] Worker created." << std::endl;
    }
    
    int run(spitz::istream& task, const spitz::pusher& result)
    {
        struct timeval time_start;
        gettimeofday(&time_start, NULL);

        // Binary stream used to store the output
        spitz::ostream o;

        int curWidth;
        int lastWidth;

        task >> curWidth;
        task >> lastWidth; 
        o << curWidth;
        o << lastWidth;
        std::cout << "[WK] Started task columns " << curWidth << " to " << lastWidth << std::endl;
        
        int columnsCompleted = 0;
        (*rayTracer).camera.calculateWUV();
        Image image(lastWidth, (*rayTracer).height);
        uint64_t raysCast = 0;

        if ((*rayTracer).dispersion < 0) {
            (*rayTracer).depthComplexity = 1;
        }

        (*rayTracer).imageScale = (*rayTracer).camera.screenWidth / (float) (*rayTracer).width;

        for (int x = curWidth ; x < lastWidth ; x++) {
            columnsCompleted++;
            for (int y = 0; y < (*rayTracer).height; y++) {
               image.pixel(x, y, (*rayTracer).castRayForPixel(x, y, raysCast));
            }
        }
        
        o << columnsCompleted;
        o << (long) raysCast;

        for (int x = curWidth ; x < lastWidth ; x++) {
            for (int y = 0 ; y < (*rayTracer).height ; y++) {
                Color color = image.pixel(x, y);
                o.write_data(&color, sizeof(image.pixel(x, y)));
            }
        }
        // Send the result to the Spitz runtime
        result.push(o);

        struct timeval time_end;
        gettimeofday(&time_end, NULL);

        float time = time_end.tv_sec - time_start.tv_sec;
        time += (time_end.tv_usec - time_start.tv_usec)/1000000.0f;
        
        std::cout << "[WK] Columns " << curWidth << " to " << lastWidth << " processed." << std::endl;
        std::cout << "[WK] Time: " << time << " seconds" << std::endl; 

        return 0;
    }
};

// This class is responsible for merging the result of each individual task 
// and, if necessary, to produce the final result after all of the task 
// results have been received.
class committer : public spitz::committer
{
private:
    std::string outFile;
    int columnsCompleted;
    long raysCast;

    struct timeval time_start;

    Image *image;
public:
    committer(int argc, const char *argv[], spitz::istream& jobinfo)
    {
        gettimeofday(&time_start, NULL);
        if (argc > 4) {
            outFile = argv[4];
        } else {
            std::cerr << "No outFile specified - writing to out.tga" << std::endl;
            outFile = "out.tga";
        }
        columnsCompleted = 0;
        raysCast = 0;

        image = new Image(1920, 1080);

        std::cout << "[CO] Committer created." << std::endl;
    }
    
    int commit_task(spitz::istream& result)
    {
        int init_width;
        int final_width;
        int curColumns;
        long curRays;
        int imageSize;

        result >> init_width;
        result >> final_width;

        result >> curColumns;
        result >> curRays;

        columnsCompleted += curColumns;
        raysCast += curRays;

        for (int x = init_width ; x < final_width ; x++) {
            for (int y = 0 ; y < 1080 ; y++) {
                Color color;
                result.read_data(&color, sizeof(color));
                (*image).pixel(x, y, color);
            }
        }

        float percentage = columnsCompleted/(float)1920 * 100;
        struct timeval this_commit;
        gettimeofday(&this_commit, NULL);

        float sofar = this_commit.tv_sec - time_start.tv_sec;
        sofar += (this_commit.tv_usec - time_start.tv_usec)/1000000.0f;
        float expected = ((sofar * 1920)/(columnsCompleted)) - sofar;

        std::cout << "[CO] Percentage completed: " << percentage << "%" << std::endl;
        std::cout << "[CO] Time to finish: " << expected << " seconds" << std::endl;
        std::cout << "[CO] Rays cast: " << raysCast << std::endl;

        std::cout << "[CO] Columns " << init_width << " to " << final_width << " committed." << std::endl;
        return 0;
    }
    
    ~committer()
    {
        (*image).WriteTga(outFile.c_str(), false);
        struct timeval final_commit;
        gettimeofday(&final_commit, NULL);

        float time = final_commit.tv_sec - time_start.tv_sec;
        time += (final_commit.tv_usec - time_start.tv_usec)/1000000.0f;

        std::cout << "[CO-Final] Rays cast: " << raysCast << std::endl;
        std::cout << "[CO-Final] Time: " << time << " seconds" << std::endl;
        std::cout << "[CO-Final] Performance: " << raysCast/time << " rays/sec" << std::endl;
    }
};

// The factory binds the user code with the Spitz C++ wrapper code.
class factory : public spitz::factory
{
public:
    spitz::job_manager *create_job_manager(int argc, const char *argv[],
        spitz::istream& jobinfo)
    {
        return new job_manager(argc, argv, jobinfo);
    }
    
    spitz::worker *create_worker(int argc, const char *argv[])
    {
        return new worker(argc, argv);
    }
    
    spitz::committer *create_committer(int argc, const char *argv[], 
        spitz::istream& jobinfo)
    {
        return new committer(argc, argv, jobinfo);
    }
};

// Creates a factory class.
spitz::factory *spitz_factory = new factory();
