//
// C++ Interface: Write to DRAM (30s flush back), read to DRAM, no duplication
//
// Description:
//
// Author: Ziqi Fan, (C) 2015
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef WDRDND_H
#define WDRDND_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

extern int totalPageWriteToStorage;

extern double priceDvsN;

extern double moneyAllo4D;

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int readHitOnNVRAM;

extern int readHitOnDRAM;

template <typename K, typename V>
class WDRDND : public TestCache<K, V>
{
public:
    typedef list<K> key_tracker_type;
    typedef map< K, V> key_to_value_type;

    WDRDND(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        assert ( _capacity!=0 );
    }

    uint32_t access(const K &k  , V &value, uint32_t status) {
        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        ///Delete this if priceDvsN is not equal to 1.0
        if(DRAM_capacity + NVM_capacity < _capacity) {
            NVM_capacity = _capacity - DRAM_capacity;
        }

        PRINTV(logfile << endl;);

        assert((t1.size() + t2.size()) <= _capacity);
        assert(t2.size() <= (unsigned)NVM_capacity);
        assert(t1.size() <= DRAM_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

// 30s flush back
        typename key_tracker_type::iterator itTracker;
        typename key_to_value_type::iterator itDirty;
        typename key_to_value_type::iterator itSeqTemp;

        // Denote the first sequential fs block number that counting consecutive pages after the victim page
        uint64_t firstSeqFsblknoForAfter = 0;
        // Denote the first sequential fs block number that counting consecutive pages before the victim page
        uint64_t firstSeqFsblknoForBefore = 0;

        int seqLength = 0;

        // time gap of every two flush backs in milliseconds
        uint32_t flushTimeGap = 30000;
        // denote how many 30s flush back has been operated
        static uint32_t multipleFlushTimeGap = 1;

        uint32_t CLEAN = ~DIRTY;

        // If the accessed entry's issueTime is the first one bigger or equal than a multiple of 30s,
        // prepare to flush back all the dirty pages residing on cache
        if(value.getReq().issueTime >= (flushTimeGap*multipleFlushTimeGap)) {
            // Loop through cache and find out those dirty pages. Group sequential ones together and log to DiskSim input trace.
            // CLEAN is used to toggle DIRTY status after the dirty page has been flushed back.
            for(itTracker = t1_key.begin(); itTracker != t1_key.end(); itTracker++) {
                itDirty = t1.find(*itTracker);
                firstSeqFsblknoForAfter = *itTracker;
                firstSeqFsblknoForBefore = *itTracker;
                seqLength = 1;

                if(itDirty->second.getReq().flags & DIRTY) {
                    // Aug 9, 2013: find the number of consecutive blocks after the selected page
                    while(true) {
                        itSeqTemp = t1.find(firstSeqFsblknoForAfter);

                        if(itSeqTemp == t1.end() || !((itSeqTemp->second.getReq().flags) & DIRTY)) {
                            break;
                        }
                        else {
                            itSeqTemp->second.updateFlags(itSeqTemp->second.getReq().flags & CLEAN);
                            firstSeqFsblknoForAfter++;
                            seqLength++;
                        }
                    }

                    // Find the number of consecutive blocks before the selected page
                    firstSeqFsblknoForBefore--;
                    while(true) {
                        itSeqTemp = t1.find(firstSeqFsblknoForBefore);

                        if(itSeqTemp == t1.end() || !((itSeqTemp->second.getReq().flags) & DIRTY)) {
                            // flush out
                            totalPageWriteToStorage += seqLength;
                            break;
                        }
                        else {
                            itSeqTemp->second.updateFlags(itSeqTemp->second.getReq().flags & CLEAN);
                            firstSeqFsblknoForBefore--;
                            seqLength++;
                        }
                    }
                    itDirty->second.updateFlags(itDirty->second.getReq().flags & CLEAN);
                }
            }
            multipleFlushTimeGap += (uint32_t(value.getReq().issueTime) - flushTimeGap*multipleFlushTimeGap) / flushTimeGap + 1;
            PRINTV(logfile << "multipleFlushTimeGap: " << multipleFlushTimeGap << endl;);
        }

        // If request is write, mark the page status as DIRTY
        if(status & WRITE) {
            status |= DIRTY;
            value.updateFlags(status);
        }
// end of 30s flush back

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1 = t1.find(k);
        const typename key_to_value_type::iterator it_t2 = t2.find(k);

        // cache hit
        if(it_t1 != t1.end()) {
            assert(it_t2 == t2.end());

            // WDRDND Case I: read x hit in DRAM, then move x to MRU of t1
            if(status & READ) {
                PRINTV(logfile << "Case I read hit on t1: " << k << endl;);

                readHitOnDRAM++;

                // preserve the hit page
                const V v = _fn(k, it_t1->second);

                t1.erase(it_t1);
                t1_key.remove(k);

                assert(t1.size() < DRAM_capacity);
                t1_key.insert(t1_key.end(), k);
                t1.insert(make_pair(k, v));
                PRINTV(logfile << "Case I read hit move key to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                return (status | PAGEHIT | BLKHIT);
            }
            // WDRDND Case II: write x hit in DRAM, if write bit @ DRAM is 1, then move x to MRU of t2, read bit @ NVRAM set to 1
            //                                      if write bit @ DRAM is 0, then set to 1
            else {
                PRINTV(logfile << "Case II write hit on t1: " << k << endl;);

                writeHitOnClean++;

                // if the current page is cold, change it to hot, mark the page to dirty
                if(it_t1->second.getFlags() & COLD) {
                    it_t1->second.updateFlags(it_t1->second.getFlags() & ~COLD);
                    it_t1->second.updateFlags(it_t1->second.getFlags() | DIRTY);
                    PRINTV(logfile << "Case II write hit on t1, write bit is 0, change the page to hot and dirty: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                    PRINTV(logfile << "after flags: " << bitset<13>(it_t1->second.getFlags()) << endl;);
                }
                // if the current page is hot, move to MRU of NVRAM, read bit @ NVRAM set to 1
                else {
                    // evict the hit page from DRAM
                    t1.erase(it_t1);
                    t1_key.remove(k);

                    // if NVRAM is full, evict the LRU page of NVRAM
                    if(t2.size() == (unsigned)NVM_capacity) {
                        ///select the LRU page of t2
                        typename key_tracker_type::iterator itLRU = t2_key.begin();
                        assert(itLRU != t2_key.end());
                        typename key_to_value_type::iterator it = t2.find(*itLRU);
                        assert(it != t2.end());

                        ///flush back and evcit the LRU page of t2
                        t2.erase(it);
                        t2_key.remove(*itLRU);
                        PRINTV(logfile << "Case II write hit on t1, NVRAM is full, evicting LRU page of t2: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                        totalPageWriteToStorage++;
                    }

                    // set the hit page to a hot page at NVRAM
                    PRINTV(logfile << "before flags: " << bitset<13>(value.getFlags()) << endl;);
                    // if the current page is cold, change it to hot
                    if(value.getFlags() & COLD) {
                        value.updateFlags(value.getFlags() & ~COLD);
                    }
                    PRINTV(logfile << "after flags: " << bitset<13>(value.getFlags()) << endl;);

                    // migrate the hitted page to MRU of t2
                    assert(t2.size() < (unsigned)NVM_capacity);
                    const V v = _fn(k, value);
                    t2_key.insert(t2_key.end(), k);
                    t2.insert(make_pair(k, v));
                    PRINTV(logfile << "Case II write hit on t1, insert to MRU of t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                }

                return (status | PAGEHIT | BLKHIT);
            }
        }

        // cache hit
        if(it_t2 != t2.end()) {
            assert(it_t1 == t1.end());

            ///ziqi: WDRDND Case III: read x hit in NVRAM, if read bit is 0 (cold) set to 1, if read bit is 1 (hot), move to MRU of DRAM, write bit @ DRAM set to 1 (hot)
            if(status & READ) {
                PRINTV(logfile << "Case III read hit on NVRAM: " << k << endl;);

                readHitOnNVRAM++;

                PRINTV(logfile << "before flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                //if the current page is cold, change it to hot
                if(it_t2->second.getFlags() & COLD) {
                    it_t2->second.updateFlags(it_t2->second.getFlags() & ~COLD);
                    PRINTV(logfile << "after flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                }
                // if the current page is hot, move to MRU of DRAM, write bit @ DRAM set to 1
                // Note that no flush is needed right now (flush at DRAM)
                else {
                    // evict the hit page from NVRAM
                    t2.erase(it_t2);
                    t2_key.remove(k);

                    // Note that no flush is needed right now (flush at DRAM)
                    /* totalPageWriteToStorage++; */

                    // if DRAM is full, evict the LRU page of DRAM
                    // Note that no checking whether the LRU page of DRAM is dirty or clean. The chance of the page is dirty is close to 0%
                    if(t1.size() == DRAM_capacity) {
                        ///select the LRU page of t1
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it = t1.find(*itLRU);
                        assert(it != t1.end());

                        ///flush back and evcit the LRU page of t1
                        t1.erase(it);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case III read hit on t2, DRAM is full, evicting LRU page of t1: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                    }

                    // change the hit page to hot and dirty
                    value.updateFlags(value.getFlags() & ~COLD);
                    value.updateFlags(value.getFlags() | DIRTY);

                    // migrate the hitted page to MRU of t1
                    assert(t1.size() < DRAM_capacity);
                    const V v = _fn(k, value);

                    t1_key.insert(t1_key.end(), k);
                    t1.insert(make_pair(k, v));
                    PRINTV(logfile << "Case III read hit on t2, insert to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                }

                return (status | PAGEHIT | BLKHIT);
            }
            //ziqi: WDRDND Case IV: write x hit in NVRAM, move to MRU of NVRAM
            else {
                PRINTV(logfile << "Case IV write hit on NVRAM: " << k << endl;);

                writeHitOnDirty++;

                // preserve the hit page
                const V v = _fn(k, it_t2->second);

                t2.erase(it_t2);
                t2_key.remove(k);
                assert(t2.size() < (unsigned)NVM_capacity);

                // Record k as most-recently-used key
                t2_key.insert(t2_key.end(), k);
                t2.insert(make_pair(k, v));
                PRINTV(logfile << "Case IV write hit on NVRAM, move key to MRU of t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                return (status | PAGEHIT | BLKHIT);
            }
        }

        // cache miss
        if( (it_t1 == t1.end()) && (it_t2 == t2.end()) ) {
            // read miss, insert to MRU of DRAM, write bit set to 0
            if(status & READ) {
                PRINTV(logfile << "Case V read miss: " << k << endl;);
                // if DRAM is full, evict the LRU page of DRAM
                // Note that no checking whether the LRU page of DRAM is dirty or clean. The chance of the page is dirty is close to 0%
                if(t1.size() == DRAM_capacity) {
                    ///select the LRU page of t1
                    typename key_tracker_type::iterator itLRU = t1_key.begin();
                    assert(itLRU != t1_key.end());
                    typename key_to_value_type::iterator it = t1.find(*itLRU);
                    assert(it != t1.end());

                    ///flush back and evcit the LRU page of t1
                    t1.erase(it);
                    t1_key.remove(*itLRU);
                    PRINTV(logfile << "Case V read miss, DRAM is full, evicting LRU page of t1: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                }

                // set the new page's write bit to cold
                value.updateFlags(value.getFlags() | COLD);

                // insert the missed page to MRU of t1
                assert(t1.size() < DRAM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, v));
                PRINTV(logfile << "Case V read miss, insert to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            }
            // write miss, insert to MRU of DRAM, write bit set to 1
            else {
                PRINTV(logfile << "Case VI write miss: " << k << endl;);
                // if DRAM is full, evict the LRU page of DRAM
                // Note that no checking whether the LRU page of DRAM is dirty or clean. The chance of the page is dirty is close to 0%
                if(t1.size() == DRAM_capacity) {
                    ///select the LRU page of t1
                    typename key_tracker_type::iterator itLRU = t1_key.begin();
                    assert(itLRU != t1_key.end());
                    typename key_to_value_type::iterator it = t1.find(*itLRU);
                    assert(it != t1.end());

                    ///flush back and evcit the LRU page of t1
                    t1.erase(it);
                    t1_key.remove(*itLRU);
                    PRINTV(logfile << "Case VI write miss, DRAM is full, evicting LRU page of t1: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                }

                // set the new page's write bit to hot
                value.updateFlags(value.getFlags() & ~COLD);

                // insert the missed page to MRU of t1
                assert(t1.size() < DRAM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, v));
                PRINTV(logfile << "Case VI write miss, insert to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            }
            return (status | PAGEMISS);
        }

        // should never reach here
        assert(0);
        return 0;
    } // end operator access

private:

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history DRAM LRU list
    key_tracker_type t1_key;
    ///ziqi: Key-to-value in DRAM
    key_to_value_type t1;
    ///ziqi: Key access history NVRAM LRU list
    key_tracker_type t2_key;
    ///ziqi: Key-to-value in NVRAM
    key_to_value_type t2;
};

#endif //end hybrid
