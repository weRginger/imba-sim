#include <sys/types.h>
#include <sys/stat.h>
#include "stats.h"
#include "global.h"

using namespace std;
extern StatsDS   *_gStats;

///extern int totalEvictedCleanPages;

///extern int totalSeqEvictedDirtyPages;

///extern int totalNonSeqEvictedDirtyPages;

extern int totalPageWriteToStorage;

extern int totalPageWriteDueTo30sFlush;

///ziqi test the pingpong Phenomenon of hybrid-dynamic-withpcr
extern int totalPageWriteToStorageWithPingpong;

extern int migrationNum;

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int readHitOnNVRAM;

extern int readHitOnDRAM;

extern int dirtyPageInCache;

void collectStat(int level, uint32_t newFlags)
{
    ++ _gStats[level].Ref;

    ///ziqi: find dirty page number
    if(newFlags & DIRTY) {
        ++_gStats[level].DirtyPage ;
    }

    if(newFlags & SEQEVICT) {
        ++_gStats[level].SeqEviction ;
    }

    if(newFlags & LESSSEQEVICT) {
        ++_gStats[level].LessSeqEviction ;
    }

    // find read or write count

    if(newFlags	&	READ) {
        ++_gStats[level].PageRead ;

        // Collect Read stats
        if(newFlags	&	PAGEHIT) {
            ++ _gStats[level].PageReadHit;
            assert(newFlags & BLKHIT);
            assert(!(newFlags & PAGEMISS));
        }

        if(newFlags	&	PAGEMISS)
            ++ _gStats[level].PageReadMiss;

        if(newFlags	&	BLKHIT) {
            ++ _gStats[level].BlockWriteHit;
            assert(!(newFlags & BLKMISS));
        }

        if(newFlags	&	BLKMISS) {
            ++ _gStats[level].BlockReadMiss;
            ++ _gStats[level].PageReadMiss;
        }
        ///ziqi: added! read could also generate EVICT. Previously, only when newFlags is WRITE, having the following code.
        if(newFlags	&	EVICT)
            ++ _gStats[level].BlockEvict;
    }
    else if(newFlags	&	WRITE) {
        ++_gStats[level].PageWrite;

        // Collect Read stats
        if(newFlags	&	PAGEHIT) {
            ++ _gStats[level].PageWriteHit;
            assert(newFlags & BLKHIT);
            assert(!(newFlags & PAGEMISS));
        }

        if(newFlags	&	BLKHIT) {
            ++ _gStats[level].BlockWriteHit;
            assert(!(newFlags & BLKMISS));

            if(newFlags	&	PAGEMISS)
                ++ _gStats[level].PageWriteMiss;
        }

        if(newFlags	&	BLKMISS) {
            assert(!(newFlags & BLKHIT));
            ++ _gStats[level].BlockWriteMiss;
            ++ _gStats[level].PageWriteMiss;
        }

        if(newFlags	&	EVICT)
            ++ _gStats[level].BlockEvict;

        if(newFlags	&	PAGEMISS && !(newFlags	& BLKHIT) && !(newFlags	& BLKMISS))    // for page based algorithm
            ++ _gStats[level].PageWriteMiss;

        /*
            if(newFlags & COLD2COLD) {
                ++ _gStats[level].Cold2Cold;
                assert(!(newFlags & COLD2HOT));
            }
            */

        /*
        if(newFlags & COLD2HOT)
            ++ _gStats[level].Cold2Hot;
        */
    }
    else {
        cerr << "Error: Unknown request type in stat collection" << endl;
        assert(0);
    }
}

// print histograms
void printHist()
{
    //TODO: print stat for each cache in hierarchy
    int i = 0;
    ofstream pirdStream, birdStream;
    string pirdName("Stats/");
    pirdName.append(_gConfiguration.testName);
    pirdName.append("-");
    pirdName.append(_gConfiguration.GetAlgName(i));
    string birdName(pirdName);
    pirdName.append(".PIRD");
    birdName.append(".BIRD");
    pirdStream.open(pirdName, ios::out | ios::trunc);

    if(! pirdStream.good()) {
        cerr << "Error: can not open PIRD file: " << pirdName << endl;
        return;
    }

    birdStream.open(birdName, ios::out | ios::trunc);

    if(! birdStream.good()) {
        cerr << "Error: can not open BIRD file: " << birdName << endl;
        return;
    }

    for(unsigned i = 0; i < _gConfiguration.futureWindowSize  ; ++i) {
        pirdStream << i << "\t" << _gConfiguration.pirdHist[i] << endl;
        birdStream << i << "\t" << _gConfiguration.birdHist[i] << endl;
    }

    pirdStream.close();
    birdStream.close();
}

//print stats
void printStats()
{
    ofstream statStream;
    mkdir("Stats", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    string fileName("Stats/");
    fileName.append(_gConfiguration.testName);
    fileName.append(".stat");
    statStream.open(fileName, ios::out | ios::app);

    if(! statStream.good()) {
        cerr << "Error: can not open stat file: " << fileName << endl;
        return;
    }

    statStream << _gConfiguration.testName << endl;
    Stat *tempStat;

    //print stat results for each level
    for(int i = 0 ; i < _gConfiguration.totalLevels ; i++) {
        statStream << "Level " << i + 1 << ",\t" << _gConfiguration.GetAlgName(i) << endl;

        while((tempStat = _gStats[i].next())) {
            statStream << tempStat->print() << endl;
        }

        ///uint64_t blockEvict = _gStats[i].BlockEvict.getCounter();
        ///uint64_t seqEviction = _gStats[i].SeqEviction.getCounter();
        ///uint64_t lessSeqEviction = _gStats[i].LessSeqEviction.getCounter();

        ///statStream << "Total Seq Evicted Dirty Pages, " << totalSeqEvictedDirtyPages << endl;

        ///statStream << "Total NonSeq Evicted Dirty Pages, " << totalNonSeqEvictedDirtyPages << endl;

        statStream << "Total page write number to storage, " << totalPageWriteToStorage << endl;
	
	statStream << "Total page write number to storage due to 30s flush back, " << totalPageWriteDueTo30sFlush << endl; 

        statStream << "Total page write number to storage due to ping pong, " << totalPageWriteToStorageWithPingpong << endl;

        statStream << "Dirty pages in cache after trace running finished, " << dirtyPageInCache << endl;

        statStream << "Total write traffic (dirty page sync+dirty page in cache), " << totalPageWriteToStorage + dirtyPageInCache << endl;

        ///statStream << "Total Evicted Clean Pages, " << totalEvictedCleanPages << endl;

        statStream << "Page read cache hit ratio, " << double(_gStats[i].PageReadHit.getCounter()) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Page write cache hit ratio, " << double(_gStats[i].PageWriteHit.getCounter()) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Page write hit on dirty pages, " << double(writeHitOnDirty) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Page write hit on clean pages, " << double(writeHitOnClean) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Page read hit on NVRAM, " << double(readHitOnNVRAM) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Page read hit on DRAM, " << double(readHitOnDRAM) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Total cache hit ratio, " << (double(_gStats[i].PageReadHit.getCounter()) + double(_gStats[i].PageWriteHit.getCounter())) / double(_gStats[i].Ref.getCounter()) * 100 <<"%"<<endl;

        statStream << "Migration Number, " << migrationNum <<endl;

        statStream << endl;
    }
    statStream.close();
    IFHIST(printHist(););
}
