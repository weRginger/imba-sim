#include <iostream>
#include <time.h>
#include <deque>
#include <stdlib.h>
#include "global.h"
#include "cpp_framework.h"
#include "configuration.h"
#include "parser.h"
#include "stats.h"

#include "lru.h"

#include "darcer.h"
#include "harc.h"
#include "arc.h"
#include "lru-wsr.h"

#include "fab++.h"
#include "bplru.h"
#include "lb-clock.h"
#include "fab.h"

/*
#include "lb-arc.h"
#include "clock.h"
#include "car.h"
#include "lb-car.h"
#include "harc++.h"
#include "arc++.h"
#include "hybrid-fixed.h"
#include "hybrid-dynamic.h"
#include "hybrid-dynamic-withpcr.h"
#include "hybrid-lrulfu.h"
#include "wn-rd-nd.h"
#include "wn-rd-ad.h"
#include "wd-rd-nd.h"
#include "wd-rd-ad.h"
#include "min.h"
#include "lru-dram.h"
#include "lru_ziqi.h"
#include "lru_dynamic.h"
#include "lru_dynamicB.h"
#include "nvm_dram.h"

#include "darc.h"
#include "darc2.h"
#include "iocache.h"
#include "iocache-threshold.h"
#include "darcest.h"
#include "cflru.h"
*/

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//GLOBALS
////////////////////////////////////////////////////////////////////////////////

///int totalSeqEvictedDirtyPages;

///int totalNonSeqEvictedDirtyPages;

///int totalEvictedCleanPages;

int totalPageWriteToStorage;

int totalLargeBlockWriteToStorage;

int totalPageWriteDueTo30sFlush;

///ziqi test the pingpong Phenomenon of hybrid-dynamic-withpcr
int totalPageWriteToStorageWithPingpong;

int threshold;

int migrationNum;

unsigned int realPageAtDRAM;

int writeHitOnDirty;

int writeHitOnClean;

int readHitOnNVRAM;

int readHitOnDRAM;

int dirtyPageInCache;

// nvram size in number of pages
int nvramSize;

// set the price ratio between DRAM and NVM, e.g., 0.5 means DRAM's unit cost is half of NVM's
double priceDvsN;
// set the money allocation ratio between DRAM and total money, e.g., 0.25 means for a fixed money M, buying DRAM uses 1/4*M  and NVM 3/4*M
double moneyAllo4D;

Configuration	_gConfiguration;
bool _gTraceBased = false;
TestCache<uint64_t, cacheAtom> **_gTestCache; // pointer to each cache class in the hierachy
StatsDS *_gStats;
deque<reqAtom> memTrace; // in memory trace file


void	readTrace(deque<reqAtom> & memTrace)
{
    assert(_gTraceBased); // read from stdin is not implemented
    _gConfiguration.traceStream.open(_gConfiguration.traceName, ifstream::in);

    if(! _gConfiguration.traceStream.good()) {
        PRINT(cout << " Error: Can not open trace file : " << _gConfiguration.traceName << endl;);
        ExitNow(1);
    }

    reqAtom newAtom;
    uint32_t lineNo = 0;

    while(getAndParseMSR(_gConfiguration.traceStream , &newAtom)) {

        ///cout<<"lineNo: "<<newAtom.lineNo<<" flags: "<<newAtom.flags<<" fsblkn: "<<newAtom.fsblkno<<" issueTime: "<<newAtom.issueTime<<" reqSize: "<<newAtom.reqSize<<endl;

        ///ziqi: if writeOnly is 1, only insert write cache miss page to cache
        if(_gConfiguration.writeOnly) {



            if(newAtom.flags & WRITE) {
#ifdef REQSIZE
                uint32_t reqSize = newAtom.reqSize;
                newAtom.reqSize = 1;

                //expand large request
                for(uint32_t i = 0 ; i < reqSize ; ++ i) {
                    memTrace.push_back(newAtom);
                    ++ newAtom.fsblkno;
                }

#else
                memTrace.push_back(newAtom);
#endif
            }
        }
        ///ziqi: if writeOnly is 0, insert both read & write cache miss page to cache
        else {
#ifdef REQSIZE
            uint32_t reqSize = newAtom.reqSize;
            newAtom.reqSize = 1;

            //expand large request
            for(uint32_t i = 0 ; i < reqSize ; ++ i) {
                memTrace.push_back(newAtom);
                ++ newAtom.fsblkno;
            }

#else
            memTrace.push_back(newAtom);
#endif
        }

        assert(lineNo < newAtom.lineNo);
        IFDEBUG(lineNo = newAtom.lineNo;);
        newAtom.clear();
    }

    _gConfiguration.traceStream.close();
}

void	Initialize(int argc, char **argv, deque<reqAtom> & memTrace)
{
    if(!_gConfiguration.read(argc, argv)) {
        cerr << "USAGE: <TraceFilename> <CfgFileName> <TestName>" << endl;
        exit(-1);
    }

    readTrace(memTrace);
    assert(memTrace.size() != 0);
    //Allocate StatDs
    _gStats = new StatsDS[_gConfiguration.totalLevels];
    //Allocate hierarchy
    _gTestCache = new TestCache<uint64_t, cacheAtom>*[_gConfiguration.totalLevels];

    for(int i = 0; i < _gConfiguration.totalLevels ; i++) {
        if(_gConfiguration.GetAlgName(i).compare("lru") == 0) {
            _gTestCache[i] = new LRU<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("darcer") == 0) {
            _gTestCache[i] = new HARC<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("h-arc") == 0) {
            _gTestCache[i] = new HARC<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("fab++") == 0) {
            _gTestCache[i] = new FABPlusPlus<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], _gConfiguration.ssd2fsblkRatio[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("lb-clock") == 0) {
            _gTestCache[i] = new LBCLOCK<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], _gConfiguration.ssd2fsblkRatio[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("bplru") == 0) {
            _gTestCache[i] = new BPLRU<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], _gConfiguration.ssd2fsblkRatio[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("fab") == 0) {
            _gTestCache[i] = new FAB<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], _gConfiguration.ssd2fsblkRatio[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("arc") == 0) {
            _gTestCache[i] = new ARC<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
        }
        else if(_gConfiguration.GetAlgName(i).compare("lru-wsr") == 0) {
            _gTestCache[i] = new LRUWSRCache<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
        }
        /*
        *else if(_gConfiguration.GetAlgName(i).compare("lb-arc") == 0) {
          _gTestCache[i] = new LBARC<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], _gConfiguration.ssd2fsblkRatio[i], i);
          }
              else if(_gConfiguration.GetAlgName(i).compare("clock") == 0) {
                  _gTestCache[i] = new CLOCK<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
              }
              else if(_gConfiguration.GetAlgName(i).compare("car") == 0) {
                  _gTestCache[i] = new CAR<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
              }
                  else if(_gConfiguration.GetAlgName(i).compare("wnrdnd") == 0) {
                      _gTestCache[i] = new WNRDND<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("wnrdad") == 0) {
                      _gTestCache[i] = new WNRDAD<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("wdrdnd") == 0) {
                      _gTestCache[i] = new WDRDND<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("wdrdad") == 0) {
                      _gTestCache[i] = new WDRDAD<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }

              else if(_gConfiguration.GetAlgName(i).compare("harc") == 0) {
              _gTestCache[i] = new HARC<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
              }
              else if(_gConfiguration.GetAlgName(i).compare("pagemin") == 0) {
              _gTestCache[i] = new PageMinCache(cacheAll, _gConfiguration.cacheSize[i], i);
              }
              else if(_gConfiguration.GetAlgName(i).compare("lru-dram") == 0) {
              _gTestCache[i] = new LRU4DRAM<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
              }
                  else if(_gConfiguration.GetAlgName(i).compare("hybrid-fixed") == 0) {
                      _gTestCache[i] = new HybridFixed<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("hybrid-dynamic") == 0) {
                      _gTestCache[i] = new HybridDynamic<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("hybrid-lrulfu") == 0) {
                      _gTestCache[i] = new HybridLRULFU<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("hybrid-dynamic-withpcr") == 0) {
                      _gTestCache[i] = new HybridDynamicWithPCR<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("iocache") == 0) {
                      _gTestCache[i] = new IOCACHE<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("iocache-threshold") == 0) {
                      _gTestCache[i] = new IOCACHE_THRESHOLD<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("darcest") == 0) {
                      _gTestCache[i] = new DARCEST<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("cflru") == 0) {
                      _gTestCache[i] = new CFLRU<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("blockmin") == 0) {
                      _gTestCache[i] = new BlockMinCache(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("ziqilru") == 0) {
                      _gTestCache[i] = new ZiqiLRUCache<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("dynamiclru") == 0) {
                      _gTestCache[i] = new DynamicLRUCache<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("dynamicBlru") == 0) {
                      _gTestCache[i] = new DynamicBLRUCache<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  else if(_gConfiguration.GetAlgName(i).compare("nvm-dram") == 0) {
                      _gTestCache[i] = new NVMDRAMCache<uint64_t, cacheAtom>(cacheAll, _gConfiguration.cacheSize[i], i);
                  }
                  */

        //esle if //add new policy name and dynamic allocation here
        else {
            cerr << "Error: UnKnown Algorithm name " << endl;
            exit(1);
        }
    }

    PRINTV(logfile << "Configuration and setup done" << endl;);
    srand(0);
}
void reportProgress()
{
    static uint64_t totalTraceLines = memTrace.size();
    static int lock = -1;
    int completePercent = ((totalTraceLines - memTrace.size()) * 100) / totalTraceLines ;

    if(completePercent % 10 == 0 && lock != completePercent) {
        lock = completePercent ;
        std::cerr << "\r--> " << completePercent << "% done" << flush;
    }

    if(completePercent == 100)
        std::cerr << endl;
}

/*backup
void recordOutTrace( int level, reqAtom newReq){
	if(_gConfiguration.outTraceStream[level].is_open()){
		_gConfiguration.outTraceStream[level] << newReq.issueTime << "," <<"OutLevel"<<level<<",0,";

		//_gConfiguration.outTraceStream[level] <<"flags: "<< newReq.flags <<" !";

		if(newReq.flags & READ){
			_gConfiguration.outTraceStream[level] << "Read,";
		}
		else
			_gConfiguration.outTraceStream[level] << "Write,";
		//FIXME: check math
		_gConfiguration.outTraceStream[level] << newReq.fsblkno * 512 * 8 <<","<< newReq.reqSize * 512 << endl;

	}
}
*/

///ziqi: this has no use by Jun 19 2013
void recordOutTrace(int level, reqAtom newReq)
{
    /*
      if(_gConfiguration.outTraceStream[level].is_open()) {
          _gConfiguration.outTraceStream[level] << newReq.issueTime << "!";
          //_gConfiguration.outTraceStream[level] <<"flags: "<< newReq.flags <<" !";

          if(newReq.flags & READ){
          	_gConfiguration.outTraceStream[level] << "Read,";
          }
          else
          	_gConfiguration.outTraceStream[level] << "Write,";

          //FIXME: check math
          _gConfiguration.outTraceStream[level] << newReq.fsblkno << endl;
          //_gConfiguration.outTraceStream[level] << newReq.flags << endl;
      }
    */
}

void runDiskSim()
{
    std::string command = _gConfiguration.diskSimPath;
    command += _gConfiguration.diskSimuExe;
    command += " ";
    command += _gConfiguration.diskSimPath;
    command += _gConfiguration.diskSimParv;
    command += " ";
    command += _gConfiguration.diskSimPath;
    command += _gConfiguration.diskSimOutv;
    command += " ascii ";

    //command += _gConfiguration.cache2diskPipeFileName;
    ///ziqi: the line above is by Alireza. I use diskSimInputTraceName to denote the DiskSim input trace file name
    command += _gConfiguration.diskSimInputTraceName;

    command += " 0";
    PRINTV(logfile << "Running Disk Simulator with following command:" << endl;);
    PRINTV(logfile << command << endl;);
    system(command.c_str());
}

void runSeqLengthAnalysis()
{
    std::string command = _gConfiguration.analysisAppPath;
    command += _gConfiguration.analysisAppExe;
    command += " ";
    command += _gConfiguration.diskSimInputTraceName;
    command += " ";
    command += "analyzed-"+_gConfiguration.diskSimInputTraceName;

    PRINTV(logfile << "Running Seq Length Analysis App with following command:" << endl;);
    PRINTV(logfile << command << endl;);
    system(command.c_str());

}

void RunBenchmark(deque<reqAtom> & memTrace)
{
    PRINTV(logfile << "Start benchmarking" << endl;);

    while(! memTrace.empty()) {
        uint32_t newFlags = 0;
        reqAtom newReq = memTrace.front();
        cacheAtom newCacheAtom(newReq);

        //access hierachy from top layer
        for(int i = 0 ; i < _gConfiguration.totalLevels ; i++) {
            newFlags = _gTestCache[i]->access(newReq.fsblkno, newCacheAtom, newReq.flags);
            collectStat(i, newFlags);

            if(newFlags & PAGEHIT)
                break; // no need to check further down in the hierachy

            recordOutTrace(i, newReq);
            newFlags = 0; // reset flag
        }

        memTrace.pop_front();
        reportProgress();
    }

    if(! _gConfiguration.diskSimuExe.empty()) {
        PRINTV(logfile << "Multi-level Cache Simulation is Done, Start Timing Simulation with Disk simulator" << endl;);
        runDiskSim();
    }

    if(! _gConfiguration.analysisAppExe.empty()) {
        PRINTV(logfile << "Timing Simulation is Done, Start Sequential Length Analysis" << endl;);
        runSeqLengthAnalysis();
    }

    PRINTV(logfile << "Benchmarking Done" << endl;);
}

int main(int argc, char **argv)
{
    ///totalEvictedCleanPages = 0;
    ///totalSeqEvictedDirtyPages = 0;
    ///totalNonSeqEvictedDirtyPages = 0;
    totalPageWriteToStorage = 0;
    totalLargeBlockWriteToStorage = 0;
    totalPageWriteDueTo30sFlush = 0;
    totalPageWriteToStorageWithPingpong = 0;
    migrationNum = 0;
    realPageAtDRAM = 0;

    writeHitOnDirty = 0;
    writeHitOnClean = 0;
    readHitOnNVRAM = 0;
    readHitOnDRAM = 0;

    dirtyPageInCache = 0;
    //read benchmark configuration
    Initialize(argc, argv, memTrace);

    if(_gConfiguration.GetAlgName(0).compare("lru") == 0
            ||_gConfiguration.GetAlgName(0).compare("arc") == 0
            ||_gConfiguration.GetAlgName(0).compare("harc") == 0
            ||_gConfiguration.GetAlgName(0).compare("clock") == 0
            ||_gConfiguration.GetAlgName(0).compare("lb-clock") == 0
            ||_gConfiguration.GetAlgName(0).compare("car") == 0
            ||_gConfiguration.GetAlgName(0).compare("lb-car") == 0
            ||_gConfiguration.GetAlgName(0).compare("lb-arc") == 0
            ||_gConfiguration.GetAlgName(0).compare("lru-dram") == 0)
        /*
        ||_gConfiguration.GetAlgName(0).compare("wnrdnd") == 0
        ||_gConfiguration.GetAlgName(0).compare("wnrdad") == 0
        ||_gConfiguration.GetAlgName(0).compare("wdrdnd") == 0
        ||_gConfiguration.GetAlgName(0).compare("wdrdad") == 0
        ||_gConfiguration.GetAlgName(0).compare("hybrid-dynamic") == 0
        ||_gConfiguration.GetAlgName(0).compare("hybrid-dynamic-withpcr") == 0
        ||_gConfiguration.GetAlgName(0).compare("hybrid-fixed") == 0
        ||_gConfiguration.GetAlgName(0).compare("hybrid-lrulfu") == 0

        ||_gConfiguration.GetAlgName(0).compare("lru-wsr") == 0
        ||_gConfiguration.GetAlgName(0).compare("darc") == 0
        ||_gConfiguration.GetAlgName(0).compare("cflru") == 0
        ||_gConfiguration.GetAlgName(0).compare("nvm-dram") == 0
        ||_gConfiguration.GetAlgName(0).compare("iocache") == 0
        ||_gConfiguration.GetAlgName(0).compare("darc2") == 0
        */
    {
        threshold = 1;
    }
    ///used by lru_ziqi, iocache-threshold
    else
        threshold = _gConfiguration.seqThreshold;


    if(_gConfiguration.GetAlgName(0).compare("nvm-dram") == 0
            ||_gConfiguration.GetAlgName(0).compare("hybrid-dynamic") == 0
            ||_gConfiguration.GetAlgName(0).compare("hybrid-dynamic-withpcr") == 0
            ||_gConfiguration.GetAlgName(0).compare("hybrid-fixed") == 0
            ||_gConfiguration.GetAlgName(0).compare("hybrid-lrulfu") == 0
            ||_gConfiguration.GetAlgName(0).compare("wnrdnd") == 0
            ||_gConfiguration.GetAlgName(0).compare("wnrdad") == 0
            ||_gConfiguration.GetAlgName(0).compare("wdrdnd") == 0
            ||_gConfiguration.GetAlgName(0).compare("wdrdad") == 0)
    {
        nvramSize = _gConfiguration.NvramSize;
        //priceDvsN = _gConfiguration.priceDRAMvsNVM;
        //moneyAllo4D = _gConfiguration.moneyAllocation4DRAM;
    }


    RunBenchmark(memTrace); // send reference memTrace
    ExitNow(0);
}
