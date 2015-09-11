//
// C++ Interface: lru_stl
//
// Description:
//
//
// Author: ARH,,, <arh@aspire-one>, (C) 2011
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef NVM_DRAM_H
#define NVM_DRAM_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

extern int migrationNum;

///the real page stored at DRAM, did not consider PCR pages
extern unsigned int realPageAtDRAM;

extern int totalPageWriteToStorage;

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int dirtyPageInCache;

extern double priceDvsN;

extern double moneyAllo4D;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class NVMDRAMCache : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > key_to_value_type;
// Constuctor specifies the cached function and
// the maximum number of records to be stored.
    NVMDRAMCache(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        ///ARH: Commented for single level cache implementation
//         assert ( _capacity!=0 );
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        ///ziqi: _capacity is the RAM size if only buying DRAM
        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;
        PRINTV(logfile << "priceDvsN: "<< priceDvsN << endl;);
        PRINTV(logfile << "moneyAllo4D: " << moneyAllo4D << endl;);
        PRINTV(logfile << "DRAM_capacity: "<< DRAM_capacity << endl;);
        PRINTV(logfile << "NVM_capacity: " << NVM_capacity << endl;);
        assert( (_key_to_value_write.size() + realPageAtDRAM) <= (DRAM_capacity + NVM_capacity) );
        assert(_capacity != 0);
        PRINTV(logfile << "******************************************************"<< endl;);
        PRINTV(logfile << "Access key: " << k << endl;);

        dirtyPageInCache = int(_key_to_value_write.size());
        PRINTV(logfile << "Dirty Page Number In Cache: " << dirtyPageInCache << endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_read	= _key_to_value_read.find(k);
        const typename key_to_value_type::iterator it_write	= _key_to_value_write.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);
        ///ziqi: if it is not hit on both lists, it is a miss
        if( (it_read == _key_to_value_read.end()) && (it_write == _key_to_value_write.end()) ) {
            // We don’t have it:
            PRINTV(logfile << "Miss on key: " << k << endl;);
            // Evaluate function and create new record
            const V v = _fn(k, value);
            ///ziqi: inserts new elements on read and write miss
            status |=  insert(k, v);
            PRINTV(logfile << "Insert done on key: " << k << endl;);
            PRINTV(logfile << "DRAM Cache utilization: " << realPageAtDRAM <<"/"<<DRAM_capacity <<endl;);
            PRINTV(logfile << "DRAM Cache total size: " << _key_to_value_read.size() <<endl;);
            PRINTV(logfile << "NVM Cache utilization: " << _key_to_value_write.size() <<"/"<<NVM_capacity <<endl;);
            return (status | PAGEMISS);
        }
        ///ziqi: if hit on lru list for write, do sth based on it's a read or write request
        else if (it_write != _key_to_value_write.end()) {
            if(status & WRITE) {
                PRINTV(logfile << "Write hit on lru list for write on key: " << k << endl;);
                PRINTV(logfile << "Migrate the page from current to MRU " << endl;);

                _key_to_value_write.erase(it_write);
                _key_tracker_write.remove(k);
                assert(_key_to_value_write.size() < (unsigned)NVM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = _key_tracker_write.insert(_key_tracker_write.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                _key_to_value_write.insert(make_pair(k, make_pair(v, itNew)));
                ///PRINTV(logfile << "Hitted key status: " << bitset<14>(v.getReq().flags) << endl;);

                writeHitOnDirty++;

                return (status | PAGEHIT | BLKHIT);
            }
            else {
                PRINTV(logfile << "Read hit on lru list for write on key: " << k << endl;);

                ///if the hitted page already has a PCR at lru list for read, just migrate it to MRU position
                if(it_read != _key_to_value_read.end()) {
                    PRINTV(logfile << "Have PCR before, migrate from current to MRU "<< endl;);
                    PRINTV(logfile << "DRAM Cache total size before: " << _key_to_value_read.size() <<endl;);
                    assert(it_read->second.first.getReq().flags & PCR);

                    _key_to_value_read.erase(it_read);
                    _key_tracker_read.remove(k);
                    ///maintain the page's PCR to be 1.
                    value.updateFlags(value.getReq().flags | PCR);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key

                    typename key_tracker_type::iterator itNew = _key_tracker_read.insert(_key_tracker_read.end(), k);

                    // Create the key-value entry,
                    // linked to the usage record.
                    _key_to_value_read.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "DRAM Cache total size after: " << _key_to_value_read.size() <<endl;);
                }
                ///if the hitted page did not have a PCR at lru list for read, create one and insert it to MRU position
                else {
                    PRINTV(logfile << "no PCR before, create one " <<endl;);
                    PRINTV(logfile << "DRAM Cache total size before: " << _key_to_value_read.size() <<endl;);
                    ///Set the page's PCR to be 1.
                    value.updateFlags(value.getReq().flags | PCR);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key

                    typename key_tracker_type::iterator itNew = _key_tracker_read.insert(_key_tracker_read.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    _key_to_value_read.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "DRAM Cache total size after: " << _key_to_value_read.size() <<endl;);
                }
                return (status | PAGEHIT | BLKHIT);
            }
        }
        ///ziqi: if hit on lru list for read, do sth based on it's a read or write request
        else {
            if(status & WRITE) {
                PRINTV(logfile << "Write hit on lru list for read on key: " << k << endl;);

                ///mark the hited page to be a PCR and the real page number at DRAM decrease by 1
                PRINTV(logfile << "Key status before change: " << bitset<14>(it_read->second.first.getReq().flags) << endl;);
                it_read->second.first.updateFlags(it_read->second.first.getReq().flags | PCR);
                PRINTV(logfile << "Key status after change: " << bitset<14>(it_read->second.first.getReq().flags) << endl;);
                realPageAtDRAM--;

                typename key_tracker_type::iterator itTracker_write;

                ///ziqi: store the LRU page at the lru list for write
                typename key_to_value_type::iterator itValue_Write;
                ///ziqi: store the PCR page of itValue_Write at the lru list for read
                typename key_to_value_type::iterator itValue_Read;

                itTracker_write = _key_tracker_write.begin();
                itValue_Read = _key_to_value_read.find(*itTracker_write);
                itValue_Write = _key_to_value_write.find(*itTracker_write);

                ///if NVM is full, evict and sync the LRU page, check whether to migrate it to DRAM
                if(_key_to_value_write.size() == (unsigned)NVM_capacity) {
                    PRINTV(logfile << "NVM if full, evict LRU page, check whether do migration " << endl;);
                    ///if the page's PCR is not located at DRAM, do no migrate
                    if(itValue_Read == _key_to_value_read.end()) {
                        PRINTV(logfile << "no PCR in lru list for read, no migration " << endl;);
                        _key_to_value_write.erase(itValue_Write);
                        _key_tracker_write.remove(*itTracker_write);
                        totalPageWriteToStorage++;
                    }
                    ///if the page's PCR is located at DRAM, migrate
                    else {
                        PRINTV(logfile << "found PCR in lru list for read, do migration " << endl;);
                        _key_to_value_write.erase(itValue_Write);
                        _key_tracker_write.remove(*itTracker_write);
                        totalPageWriteToStorage++;
                        assert(itValue_Read->second.first.getReq().flags & PCR);
                        PRINTV(logfile << "Key status before change: " << bitset<14>(it_read->second.first.getReq().flags) << endl;);
                        itValue_Read->second.first.updateFlags(itValue_Read->second.first.getReq().flags & ~PCR);
                        PRINTV(logfile << "Key status after change: " << bitset<14>(it_read->second.first.getReq().flags) << endl;);
                        realPageAtDRAM++;
                        migrationNum++;
                    }
                }
                ///insert the page into MRU position of lru list for write
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = _key_tracker_write.insert(_key_tracker_write.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                _key_to_value_write.insert(make_pair(k, make_pair(v, itNew)));

                writeHitOnClean++;

                return (status | PAGEHIT | BLKHIT);
            }
            else {
                PRINTV(logfile << "Read hit on lru list for read on key: " << k << endl;);
                PRINTV(logfile << "Migrate the page from current to MRU "<< endl;);
                _key_to_value_read.erase(it_read);
                _key_tracker_read.remove(k);
                realPageAtDRAM--;
                assert((unsigned)realPageAtDRAM < (unsigned)DRAM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = _key_tracker_read.insert(_key_tracker_read.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                _key_to_value_read.insert(make_pair(k, make_pair(v, itNew)));
                realPageAtDRAM++;
                return (status | PAGEHIT | BLKHIT);
            }
        }
    }

private:

// Record a fresh key-value pair in the cache
    int insert(const K &k, const V &v) {
        PRINTV(logfile << "insert key " << k  << endl;);
        PRINTV(logfile << "Key bit status: " << bitset<14>(v.getReq().flags) << endl;);
        ///ziqi: _capacity is the RAM size if only buying DRAM
        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        int status = 0;
        ///ziqi: if it is a write miss, insert into lru list for write
        if(v.getReq().flags & WRITE) {
            PRINTV(logfile << "Insert a write request to NVM " << endl;);
            if(_key_to_value_write.size() == (unsigned)NVM_capacity) {
                status = EVICT;
                PRINTV(logfile << "NVM Cache is Full " << _key_to_value_write.size() << " sectors" << endl;);
                PRINTV(logfile << "Starting evict from lru list for write " << endl;);

                typename key_tracker_type::iterator itTracker_write;
                typename key_tracker_type::iterator itTracker_read;

                ///ziqi: store the LRU page at the lru list for write
                typename key_to_value_type::iterator itValue_Write;
                ///ziqi: store the PCR page of itValue_Write at the lru list for read
                typename key_to_value_type::iterator itValue_Read;
                ///ziqi: store the LRU page at the lru list for read
                typename key_to_value_type::iterator itValue_Read_LRU;
                itTracker_write = _key_tracker_write.begin();
                itValue_Read = _key_to_value_read.find(*itTracker_write);
                itValue_Write = _key_to_value_write.find(*itTracker_write);
                ///this page at lru list for write has no PCR at lru list for read, just evcit it and break
                if(itValue_Read == _key_to_value_read.end()) {
                    PRINTV(logfile << "no PCR at lru list for read, evict the page at lru position of lru list for write " << endl;);
                    _key_to_value_write.erase(itValue_Write);
                    _key_tracker_write.remove(*itTracker_write);
                    totalPageWriteToStorage++;
                }
                ///this page has a PCR at lru list for read and the DRAM is not full
                else if( (itValue_Read != _key_to_value_read.end()) && ((unsigned)realPageAtDRAM < (unsigned)DRAM_capacity)) {
                    PRINTV(logfile << "have PCR and DRAM is not full, migrate to DRAM " << endl;);
                    _key_to_value_write.erase(itValue_Write);
                    _key_tracker_write.remove(*itTracker_write);
                    totalPageWriteToStorage++;
                    assert(itValue_Read->second.first.getReq().flags & PCR);
                    PRINTV(logfile << "Key status before change: " << bitset<14>(itValue_Read->second.first.getReq().flags) << endl;);
                    itValue_Read->second.first.updateFlags(itValue_Read->second.first.getReq().flags & ~PCR);
                    PRINTV(logfile << "Key status after change: " << bitset<14>(itValue_Read->second.first.getReq().flags) << endl;);
                    realPageAtDRAM++;
                    migrationNum++;
                }
                ///this page has a PCR at lru list for read and the DRAM is full
                else if( (itValue_Read != _key_to_value_read.end()) && ((unsigned)realPageAtDRAM == (unsigned)DRAM_capacity)) {
                    PRINTV(logfile << "have PCR and DRAM is full, start loop " << endl;);
                    for(itTracker_read = _key_tracker_read.begin(); itTracker_read != _key_tracker_read.end(); itTracker_read++) {
                        itValue_Read_LRU = _key_to_value_read.find(*itTracker_read);
                        if( (itValue_Read_LRU->second.first.getReq().flags & PCR) == 0) {
                            PRINTV(logfile << "the LRU page of lru list for read is real page, evict and migration " << endl;);
                            ///evict a real page from DRAM
                            _key_to_value_read.erase(itValue_Read_LRU);
                            _key_tracker_read.remove(*itTracker_read);
                            realPageAtDRAM--;
                            ///change PCR to a real page
                            PRINTV(logfile << "Key status before change: " << bitset<14>(itValue_Read->second.first.getReq().flags) << endl;);
                            itValue_Read->second.first.updateFlags(itValue_Read->second.first.getReq().flags & ~PCR);
                            PRINTV(logfile << "Key status after change: " << bitset<14>(itValue_Read->second.first.getReq().flags) << endl;);
                            realPageAtDRAM++;
                            migrationNum++;

                            ///evict from NVM
                            _key_to_value_write.erase(itValue_Write);
                            _key_tracker_write.remove(*itTracker_write);
                            totalPageWriteToStorage++;
                            break;
                        }
                        else if(itValue_Read_LRU == itValue_Read) {
                            PRINTV(logfile << "the LRU page of lru list for read is the LRU page of lru list for write, evict and no migration " << endl;);
                            assert(itValue_Read_LRU->second.first.getReq().flags & PCR);
                            ///evict from DRAM
                            _key_to_value_read.erase(itValue_Read_LRU);
                            _key_tracker_read.remove(*itTracker_read);
                            ///evict from NVM
                            _key_to_value_write.erase(itValue_Write);
                            _key_tracker_write.remove(*itTracker_write);
                            totalPageWriteToStorage++;
                            break;
                        }
                        else if( ((itValue_Read->second.first.getReq().flags & PCR) == 1) && (itValue_Read != itValue_Read_LRU) ) {
                            PRINTV(logfile << "the LRU page of lru list for read is a different PCR, evict the PCR and continue check " << endl;);
                            ///evict from DRAM
                            _key_to_value_read.erase(itValue_Read_LRU);
                            _key_tracker_read.remove(*itTracker_read);
                            continue;
                        }
                    }
                }
            }
            ///ziqi: insert it into MRU position of lru list for write
            typename key_tracker_type::iterator it = _key_tracker_write.insert(_key_tracker_write.end(), k);
            _key_to_value_write.insert(make_pair(k, make_pair(v, it)));
        }
        ///ziqi: if it is a read miss, insert into lru list for read
        else {
            PRINTV(logfile << "Insert a read request to DRAM " << endl;);
            if((unsigned)realPageAtDRAM == (unsigned)DRAM_capacity) {
                status = EVICT;
                PRINTV(logfile << "DRAM Cache is Full " << realPageAtDRAM << " sectors" << endl;);
                PRINTV(logfile << "Starting evict from lru list for read " << endl;);

                typename key_tracker_type::iterator itTracker;
                typename key_tracker_type::iterator itTrackerNext;
                typename key_to_value_type::iterator itValue;
                for(itTracker = _key_tracker_read.begin(); itTracker != _key_tracker_read.end(); itTracker++) {
                    PRINTV(logfile << "** "<< *(itTracker)<< endl;);
                }
                itTracker = _key_tracker_read.begin();
                while(itTracker != _key_tracker_read.end()) {
                    PRINTV(logfile << "1 *itTracker: " <<*itTracker<< endl;);
                    itTracker = _key_tracker_read.begin();
                    itValue = _key_to_value_read.find(*itTracker);
                    assert(itTracker != _key_tracker_read.end());
                    PRINTV(logfile << "2" << endl;);
                    assert(itValue != _key_to_value_read.end());
                    PRINTV(logfile << "3" << endl;);
                    ///ziqi: if PCR of the page at LRU position of lru list for read is 1, it is a ghost, evcit it and continue
                    if((itValue->second.first.getReq().flags) & PCR) {
                        PRINTV(logfile << "Key status: " << bitset<14>(itValue->second.first.getReq().flags) << endl;);
                        PRINTV(logfile << "Continue" << endl;);
                        PRINTV(logfile << "4" << endl;);
                        PRINTV(logfile << "size of value" << _key_to_value_read.size()<< endl;);
                        PRINTV(logfile << "size of tracker" << _key_tracker_read.size()<< endl;);
                        _key_to_value_read.erase(itValue);
                        PRINTV(logfile << "5" << endl;);
                        ///itTrackerNext = ++itTracker;
                        ///PRINTV(logfile << "6 itTrackerNext: "<< *(itTrackerNext)<< endl;);
                        ///PRINTV(logfile << "6 itTrackerNext: "<< *(itTracker++)<< endl;);
                        ///PRINTV(logfile << "6 itTrackerNext: "<< *(itTracker++)<< endl;);
                        ///PRINTV(logfile << "6 itTrackerNext: "<< *(itTracker++)<< endl;);
                        _key_tracker_read.remove(*itTracker);
                    }
                    ///ziqi: if PCR of the page at LRU position of lru list for read is 0, it is a read page, evcit it and break
                    else {
                        PRINTV(logfile << "Key status: " << bitset<14>(itValue->second.first.getReq().flags) << endl;);
                        PRINTV(logfile << "Break" << endl;);
                        _key_to_value_read.erase(itValue);
                        _key_tracker_read.remove(*itTracker);
                        realPageAtDRAM--;
                        break;
                    }
                    ///itTracker = itTrackerNext;
                    ///PRINTV(logfile << "7 itTracker: "<< *itTracker<< endl;);
                }
            }
            ///insert it into MRU position of lru list for read
            typename key_tracker_type::iterator it = _key_tracker_read.insert(_key_tracker_read.end(), k);
            _key_to_value_read.insert(make_pair(k, make_pair(v, it)));
            realPageAtDRAM++;
        }

        return status;
    }

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

// Key access history
    key_tracker_type _key_tracker_read;
// Key-to-value lookup
    key_to_value_type _key_to_value_read;
// Key access history
    key_tracker_type _key_tracker_write;
// Key-to-value lookup
    key_to_value_type _key_to_value_write;
    unsigned levelMinusMinus;
};

#endif //end lru_stl
