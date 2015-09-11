#ifndef __CONFIGURATION__
#define __CONFIGURATION__

#include <string>
#include <ctime>
#include <cassert>
#include <iostream>
#include <fstream>
#include <string.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>
#include "cpp_framework.h"

#ifdef HIST
#define IFHIST(X)	do { X	}while(false)
#else
#define IFHIST(X)
#endif


extern bool _gTraceBased;


class Configuration
{
public:
    char *traceName;
    std::ifstream traceStream;
    std::ofstream logStream;

    ///ziqi: diskSimInputStream is defined for logging dirty pages evicted from buffer cache and needed to be flushed back to storage.
    ///It works as an input trace file for DiskSim.
    std::ofstream diskSimInputStream;
    std::ofstream afterCacheTraceStream;

    std::ofstream *outTraceStream;
    std::string *policyName;
    char   *testName;
    uint64_t *cacheSize;   // in pages
    uint64_t fsblkSize;
    uint64_t *cachePageSize;
    uint64_t *cacheBlkSize;
    uint32_t *ssd2fsblkRatio;
    uint64_t maxLineNo;
    uint32_t futureWindowSize;
    uint64_t *birdHist;
    uint64_t *pirdHist;
    int 	totalLevels;
    bool writeOnly;
    std::string diskSimuExe;
    std::string diskSimPath;
    std::string diskSimParv;
    std::string diskSimOutv;
    std::string cache2diskPipeFileName;

    ///ziqi
    std::string diskSimInputTraceName;
    std::string afterCacheTraceName;
    std::string analysisAppExe;
    std::string analysisAppPath;


    ///ziqi: set the threshold for length of sequential write
    int seqThreshold;

    ///ziqi: set the price ratio between DRAM and NVM, e.g., 0.5 means DRAM's unit cost is half of NVM's
    ///set the money allocation ratio between DRAM and total money, e.g., 0.25 means for a fixed money M, buying DRAM uses 1/4*M  and NVM 3/4*M
    double priceDRAMvsNVM;
    double moneyAllocation4DRAM;

    void initHist();
    bool read(int argc, char **argv) ;
    Configuration();
    ~Configuration();

    inline std::string GetAlgName(int i) {
        if(policyName[i].find("owbp") != std::string::npos) {
            std::ostringstream convert;
            convert << futureWindowSize << "/" << cacheSize[i] ;
            return std::string(policyName[i] +  "-" + (convert.str()));
        }
        else
            return policyName[i];
    }

    inline std::string PrintTestName() {
        return std::string(testName);
    }

    inline std::string GetTraceName() {
        return std::string(traceName);
    }


private:
    void allocateArrays(int totalLevels);
    uint64_t myString2intConverter(std::string temp);
};



#endif
