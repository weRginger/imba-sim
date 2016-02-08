//
// C++ Interface: Write to NVRAM, read to DRAM, allow duplication
//
// Description:
//
// Author: Ziqi Fan, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef WNRDAD_H
#define WNRDAD_H

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
class WNRDAD : public TestCache<K, V>
{
public:
    typedef list<K> key_tracker_type;
    typedef map< K, V> key_to_value_type;

    WNRDAD(
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

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1 = t1.find(k);
        const typename key_to_value_type::iterator it_t2 = t2.find(k);

        // cache hit
        if(it_t1 != t1.end()) {
            // WNRDAD Case I: read x hit in DRAM, then move x to MRU of t1 (do not care whether a copy at NVRAM)
            if(status & READ) {
                PRINTV(logfile << "Case I read hit on t1: " << k << endl;);

                readHitOnDRAM++;

                // preserve the hit page flags
                const V v = _fn(k, it_t1->second);

                t1.erase(it_t1);
                t1_key.remove(k);

                assert(t1.size() < DRAM_capacity);
                t1_key.insert(t1_key.end(), k);
                t1.insert(make_pair(k, v));
                PRINTV(logfile << "Case I read hit move key to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                return (status | PAGEHIT | BLKHIT);
            }
            //ziqi: WNRDAD Case II: write hit in DRAM (no copy at NVRAM), then copy x to MRU of t2, read bit @ NVRAM set to 0
            else if (it_t2 == t2.end()) {
                PRINTV(logfile << "Case II write hit on t1: " << k << endl;);

                writeHitOnClean++;

                // no need to evict the hit page from DRAM

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

                // set the hit page to a cold page at NVRAM
                PRINTV(logfile << "before flags: " << bitset<13>(value.getFlags()) << endl;);
                value.updateFlags(value.getFlags() | COLD);
                PRINTV(logfile << "after flags: " << bitset<13>(value.getFlags()) << endl;);

                // copy the hitted page to MRU of t2
                assert(t2.size() < (unsigned)NVM_capacity);

                // preserve the hit page flags
                const V v = _fn(k, it_t1->second);

                t2_key.insert(t2_key.end(), k);
                t2.insert(make_pair(k, v));
                PRINTV(logfile << "Case II write hit on t1, no eviction at t1, copy to MRU of t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                return (status | PAGEHIT | BLKHIT);
            }
        }

        // cache hit
        if(it_t2 != t2.end()) {
            // WNRDAD Case III: read hit in NVRAM, no copy at DRAM, if cold set to hot, if hot, copy to MRU of DRAM, then set to cold
            if( (it_t1 == t1.end()) && (status & READ) ) {
                PRINTV(logfile << "Case III read hit on NVRAM: " << k << endl;);

                readHitOnNVRAM++;

                PRINTV(logfile << "before flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                // if the current page is cold, change it to hot
                if(it_t2->second.getFlags() & COLD) {
                    it_t2->second.updateFlags(it_t2->second.getFlags() & ~COLD);
                    PRINTV(logfile << "after flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                }
                // if the current page is hot, copy to MRU of DRAM, set the page to cold
                else {

                    // no need to evict the hit page from NVRAM

                    // if DRAM is full, evict the LRU page of DRAM
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

                    // migrate the hitted page to MRU of t1
                    assert(t1.size() < DRAM_capacity);

                    // preserve the hit page flags
                    const V v = _fn(k, it_t2->second);

                    t1_key.insert(t1_key.end(), k);
                    t1.insert(make_pair(k, v));
                    PRINTV(logfile << "Case III read hit on t2, copy to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                    // set the hit page to a cold page at NVRAM
                    PRINTV(logfile << "before flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                    it_t2->second.updateFlags(it_t2->second.getFlags() | COLD);
                    PRINTV(logfile << "after flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                }

                return (status | PAGEHIT | BLKHIT);
            }
            //ziqi: WNRDAD Case IV: write x hit in NVRAM, move to MRU of NVRAM, if a copy in DRAM, copy to DRAM
            else {
                PRINTV(logfile << "Case IV write hit on NVRAM: " << k << endl;);

                writeHitOnDirty++;

                // preserve the hit page
                const V v = _fn(k, it_t2->second);

                t2.erase(it_t2);
                t2_key.remove(k);
                assert(t2.size() < (unsigned)NVM_capacity);

                t2_key.insert(t2_key.end(), k);
                t2.insert(make_pair(k, v));
                PRINTV(logfile << "Case IV write hit on NVRAM, move key to MRU of t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                // if a copy in DRAM exists, copy it to DRAM so that the DRAM copy will be up-to-date
                // however, for simulation, nothing needs to be done

                return (status | PAGEHIT | BLKHIT);
            }
        }

        // cache miss
        if( (it_t1 == t1.end()) && (it_t2 == t2.end()) ) {
            // read miss, insert to MRU of DRAM
            if(status & READ) {
                PRINTV(logfile << "Case V read miss: " << k << endl;);
                // if DRAM is full, evict the LRU page of DRAM
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

                // insert the hitted page to MRU of t1
                assert(t1.size() < DRAM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, v));
                PRINTV(logfile << "Case V read miss, insert to MRU of t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            }
            // write miss, insert to MRU of NVRAM
            else {
                PRINTV(logfile << "Case VI write miss: " << k << endl;);
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
                    PRINTV(logfile << "Case VI write miss, NVRAM is full, evicting LRU page of t2: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
                    totalPageWriteToStorage++;
                }

                // set the missed page to a cold page at NVRAM
                value.updateFlags(value.getFlags() | COLD);
                PRINTV(logfile << "flags: " << bitset<13>(value.getFlags()) << endl;);

                // migrate the hitted page to MRU of t2
                assert(t2.size() < (unsigned)NVM_capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t2.insert(make_pair(k, v));
                PRINTV(logfile << "Case VI write miss, insert to MRU of t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
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