//
// C++ Interface: hybrid
//
// Description: Cache policy for hybrid main memory using NVRAM and DRAM. It can self-adjust to workload based on DARC
//
// Author: Ziqi Fan, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef HybridDynamic_H
#define HybridDynamic_H

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

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int dirtyPageInCache;

extern double priceDvsN;

extern double moneyAllo4D;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class HybridDynamic : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;

    // Constuctor specifies the cached function and
    // the maximum number of records to be stored.
    HybridDynamic(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        //ARH: Commented for single level cache implementation
        //assert ( _capacity!=0 );
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        ///Delete this if priceDvsN is not equal to 1.0
        if(DRAM_capacity + NVM_capacity < _capacity) {
            NVM_capacity = _capacity - DRAM_capacity;
        }
        PRINTV(logfile << endl;);
        ///ziqi: p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1a.size() + t1b.size() + t2.size()) <= _capacity);
        assert((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert(t2.size() <= (unsigned)NVM_capacity);
        assert(t1b.size() <= (unsigned)NVM_capacity);
        assert(t1a.size() <= DRAM_capacity);
        assert((t1a.size() + t1b.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        ///Keep track of the number of dirty page in cache
        ///in order to know how many pages did not have the chance to be flushed
        dirtyPageInCache = int(t2.size());
        PRINTV(logfile << "Dirty Page Number In Cache: " << dirtyPageInCache << endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1a	= t1a.find(k);
        const typename key_to_value_type::iterator it_t1b	= t1b.find(k);
        const typename key_to_value_type::iterator it_t2	= t2.find(k);
        const typename key_to_value_type::iterator it_b1	= b1.find(k);
        const typename key_to_value_type::iterator it_b2	= b2.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);
        ///ziqi: HYBRID-Dynamic Case I: x hit in t2, then move x to MRU of t2
        if(it_t2 != t2.end()) {
            PRINTV(logfile << "Case I hit on t2 with key: " << k <<endl;);
            t2.erase(it_t2);
            t2_key.remove(k);
            assert(t2.size() < _capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
            t2.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case I insert key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            return (status | PAGEHIT | BLKHIT);
        }

        ///ziqi: HYBRID-Dynamic Case II: x hit in t1, then move x to t1 if it's a read, or to t2 if it's a write
        if((it_t1a != t1a.end()) || (it_t1b != t1b.end())) {
            assert(!((it_t1a != t1a.end()) && (it_t1b != t1b.end())));
            ///ziqi: if it is a write request
            if(status & WRITE) {
                if(it_t1a != t1a.end()) {
                    ///evict the hitted clean page from t1a
                    t1a.erase(it_t1a);
                    t1_key.remove(k);

                    ///assert(t1.size() < _capacity);
                    const V v = _fn(k, value);
                    if(t2.size()+t1b.size() < (unsigned)NVM_capacity) {
                        PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is not full: " << k << endl;);
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew
                        = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                        if(t2.size() == (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() == NVRAM: " << k << endl;);
                            ///select the LRU page of t2
                            typename key_tracker_type::iterator itLRU = t2_key.begin();
                            assert(itLRU != t2_key.end());
                            typename key_to_value_type::iterator it = t2.find(*itLRU);
                            assert(it != t2.end());
                            ///insert the LRU page for t2 to t1a, since t1a has at least one page space left
                            ///Note that we insert the page from t2 to the LRU position of t1, it is NOT MRU position
                            typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, it->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                            PRINTV(logfile << "Case II insert key to t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            ///flush back and evcit the LRU page of t2
                            t2.erase(it);
                            t2_key.remove(*itLRU);
                            PRINTV(logfile << "Case II (NVM is filled with dirty pages) evicting t2 and flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            totalPageWriteToStorage++;

                            ///migrate the hitted page to MRU of t2
                            // Record k as most-recently-used key
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                        if(t2.size() < (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() < NVRAM: " << k << endl;);
                            ///migrate a clean page from t1b to t1a
                            typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                            assert(it_t1b_tmp != t1b.end());
                            typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                            const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                            t1b.erase(it_t1b_tmp);
                            PRINTV(logfile << "Case II migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            ///migrate the hitted page to MRU of t2
                            // Record k as most-recently-used key
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                    }
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Write hit on t1b " << k << endl;);
                    ///evict the hitted clean from t1b
                    t1b.erase(it_t1b);
                    t1_key.remove(k);

                    ///assert(t1.size() < _capacity);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                    = t2_key.insert(t2_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            ///ziqi: if it is a read request
            else {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II Read hit on t1a " << k << endl;);
                    t1a.erase(it_t1a);
                    t1_key.remove(k);
                    ///assert(t1.size() < _capacity);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                    = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert clean key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Read hit on t1b " << k << endl;);
                    t1b.erase(it_t1b);
                    t1_key.remove(k);
                    ///assert(t1.size() < _capacity);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                    = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert clean key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEHIT | BLKHIT);
        }

        ///ziqi: HYBRID-Dynamic Case III: x hit in b1, then enlarge t1, and move x from b1 to t1 if it's a read or to t2 if it's a write
        else if(it_b1 != b1.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case III Hit on b1: " << k << endl;);
            if(b1.size() >= b2.size()) {
                delta = 1;
            }
            else {
                if(b1.size() == 0) {
                    delta=10;
                }
                else {
                    delta = int(b2.size()/b1.size());
                }
            }

            PRINTV(logfile << "ADAPTATION: p increases from " << p << " to ";);

            if((p+delta) > int(_capacity))
                p = _capacity;
            else
                p = p+delta;

            PRINTV(logfile << p << endl;);

            const V v = _fn(k, value);
            ///E&B
            REPLACE(k, v, p, status);

            b1.erase(it_b1);
            b1_key.remove(k);

            PRINTV(logfile << "Case III evicting b1 " << k <<  endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                ///E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());
                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case III migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                assert(t2.size()+t1b.size()<(unsigned)NVM_capacity);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///ziqi: if it is a read request
            else {
                ///if t1a has space left, always insert clean page to it first
                if(t1a.size()<DRAM_capacity) {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(t1a.size() + t1b.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicting b1 " << *itLRU <<  endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                        typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);
                        if(it_t1a != t1a.end()) {
                            assert(it_t1b == t1b.end());
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU);

                            PRINTV(logfile << "Case III evicting t1a without flushing back " << *itLRU <<  endl;);
                        }
                        if(it_t1b != t1b.end()) {
                            assert(it_t1a == t1a.end());
                            t1b.erase(it_t1b);
                            t1_key.remove(*itLRU);

                            PRINTV(logfile << "Case III evicting t1b without flushing back " << *itLRU <<  endl;);
                        }
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///ziqi: HYBRID-Dynamic Case IV: x hit in b2, then enlarge t2, and move x from b2 to t1 if it's a read or to t2 if it's a write
        else if(it_b2 != b2.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case IV Hit on b2: " << k << endl;);
            if(b2.size() >= b1.size()) {
                delta = 1;
            }
            else {
                if (b2.size() == 0) {
                    delta = 10;
                }
                else {
                    delta = int(b1.size()/b2.size());
                }
            }

            PRINTV(logfile << "ADAPTATION: p decreases from " << p << " to ";);

            if((p-delta) > 0)
                p = p-delta;
            else
                p = 0;

            PRINTV(logfile << p << endl;);

            const V v = _fn(k, value);

            ///E&B
            REPLACE(k, v, p, status);

            b2.erase(it_b2);
            b2_key.remove(k);

            PRINTV(logfile << "Case IV evicting b2 " << k <<  endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                ///E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());
                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case IV migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///ziqi: if it is a read request
            else {
                ///if t1a has space left, always insert clean page to it first
                if(t1a.size()<DRAM_capacity) {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(t1a.size() + t1b.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicting b1 " << *itLRU <<  endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                        typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);
                        if(it_t1a != t1a.end()) {
                            assert(it_t1b == t1b.end());
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU);
                            PRINTV(logfile << "Case IV evicting t1a without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                        if(it_t1b != t1b.end()) {
                            assert(it_t1a == t1a.end());
                            t1b.erase(it_t1b);
                            t1_key.remove(*itLRU);
                            PRINTV(logfile << "Case IV evicting t1b without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                    }
                }
            }
            return (status | PAGEMISS);
        }

        ///ziqi: HYBRID-Dynamic Case V: x is cache miss
        else {
            PRINTV(logfile << "Case V miss on key: " << k << endl;);

            if((t1a.size() + t1b.size() + b1.size()) == _capacity) {
                if(t1a.size() + t1b.size() < _capacity) {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);

                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
                else {
                    typename key_tracker_type::iterator itLRU = t1_key.begin();
                    assert(itLRU != t1_key.end());
                    typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                    typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);
                    if(it_t1a != t1a.end()) {
                        assert(it_t1b == t1b.end());
                        t1a.erase(it_t1a);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting t1a without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    if(it_t1b != t1b.end()) {
                        assert(it_t1a == t1a.end());
                        t1b.erase(it_t1b);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting t1b without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            else if((t1a.size() + t1b.size() + b1.size()) < _capacity) {
                if((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) >= _capacity) {
                    if((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) == 2 * _capacity) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                    }

                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
            }

            const V v = _fn(k, value);
            ///write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                ///NVM is not full
                if(t1b.size() + t2.size() < (unsigned)NVM_capacity) {
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case V insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///NVM is full
                else {
                    ///t1b is empty, insert the LRU page of t2 to t1a and flush and evict the LRU page of t2
                    ///finally, move x to MRU of t2
                    if(t2.size() == (unsigned)NVM_capacity) {
                        ///select the LRU page of t2
                        typename key_tracker_type::iterator itLRU = t2_key.begin();
                        assert(itLRU != t2_key.end());
                        typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);

                        ///Note that we insert the page from t2 to the LRU position of t1, it is NOT MRU position
                        typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                        // Create the key-value entry,
                        // linked to the usage record.
                        const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                        assert(t1a.size() < DRAM_capacity);
                        t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                        PRINTV(logfile << "Case V insert key to t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        ///flush and evict the LRU page of t2
                        // Erase both elements to completely purge record
                        t2.erase(itLRUValue);
                        t2_key.remove(*itLRU);
                        totalPageWriteToStorage++;

                        PRINTV(logfile << "Case V (NVM is filled with dirty pages) evicting t2 and flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        ///migrate the hitted page to MRU of t2
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case V insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    ///t1b is not empty
                    else {
                        ///migrate a clean page from t1b to t1a
                        typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                        assert(it_t1b_tmp != t1b.end());
                        typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                        const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                        assert(t1a.size() < DRAM_capacity);
                        t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                        t1b.erase(it_t1b_tmp);
                        PRINTV(logfile << "Case V migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        ///migrate the hitted page to MRU of t2
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case V insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            ///read request
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                if(t1a.size() < DRAM_capacity) {
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case V insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                else {
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case V insert key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEMISS);
        }
        ///should never reach here
        assert(0);
        return 0;
    } //end operator access

    ///ziqi: HYBRID-Dynamic subroutine
    void REPLACE(const K &k, const V &v, int p, uint32_t status) {
        ///REPLACE can only be triggered only if both NVRAM and DRAM are full
        assert(t1a.size() + t1b.size() + t2.size() == _capacity);

        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        ///Delete this if priceDvsN is not equal to 1.0
        if(DRAM_capacity + NVM_capacity < _capacity) {
            NVM_capacity = _capacity - DRAM_capacity;
        }
        ///if NVM is not filled with dirty pages, do as the usual case
        if(t2.size() < (unsigned)NVM_capacity) {
            typename key_to_value_type::iterator it = b2.find(k);
            ///evict from clean cache side
            if((t1a.size()+t1b.size() > 0) && ( (DRAM_capacity + t1b.size() > unsigned(p) ) || ((it != b2.end()) && (DRAM_capacity + t1b.size() == unsigned(p))))) {
                typename key_tracker_type::iterator itLRU = t1_key.begin();
                assert(itLRU != t1_key.end());
                typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);
                ///if the LRU page is in t1a
                if(it_t1a != t1a.end()) {
                    assert(it_t1b == t1b.end());
                    typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                    // Create the key-value entry,
                    // linked to the usage record.
                    const V v_tmp = _fn(*itLRU, it_t1a->second.first);
                    b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
                    t1a.erase(it_t1a);
                    t1_key.remove(*itLRU);

                    PRINTV(logfile << "REPLACE (NVM not filled with dirty pages) evicting t1a without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if the LRU page is in t1b
                if(it_t1b != t1b.end()) {
                    assert(it_t1a == t1a.end());
                    typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                    // Create the key-value entry,
                    // linked to the usage record.
                    const V v_tmp = _fn(*itLRU, it_t1b->second.first);
                    b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
                    t1b.erase(it_t1b);
                    t1_key.remove(*itLRU);

                    PRINTV(logfile << "REPLACE (NVM not filled with dirty pages) evicting t1b without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            ///evcit from dirty cache side
            else {
                typename key_tracker_type::iterator itLRU = t2_key.begin();
                assert(itLRU != t2_key.end());
                typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);

                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                b2.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU <<  "** b2 size: "<< b2.size() << endl;);

                // Erase both elements to completely purge record
                PRINTV(logfile << "REPLACE evicting dirty key " << *itLRU <<  endl;);
                totalPageWriteToStorage++;
                t2.erase(itLRUValue);
                t2_key.remove(*itLRU);

                PRINTV(logfile << "REPLACE (NVM not filled with dirty pages) evicting t2 " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
        }
        ///if NVM is filled with dirty pages,
        if(t2.size() == (unsigned)NVM_capacity) {
            typename key_to_value_type::iterator it = b2.find(k);
            ///if we decide to evict from clean cache and the request is a read request, then evict the LRU page in t1a
            if( (status & READ) && ( (t1a.size()+t1b.size() > 0) && ( (DRAM_capacity + t1b.size() > unsigned(p) ) || (it != b2.end() && (DRAM_capacity + t1b.size() == unsigned(p) ) ) ) ) ) {
                typename key_tracker_type::iterator itLRU = t1_key.begin();
                assert(itLRU != t1_key.end());
                typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                assert(it_t1a != t1a.end());
                typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, it_t1a->second.first);
                b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
                t1a.erase(it_t1a);
                t1_key.remove(*itLRU);

                PRINTV(logfile << "REPLACE (NVM is filled with dirty pages) evicting t1a without flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///evcit the LRU page from dirty cache side, i.e., t2
            else {
                typename key_tracker_type::iterator itLRU = t2_key.begin();
                assert(itLRU != t2_key.end());
                typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);

                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                b2.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU <<  "** b2 size: "<< b2.size() << endl;);

                // Erase both elements to completely purge record
                PRINTV(logfile << "REPLACE evicting dirty key " << *itLRU <<  endl;);
                totalPageWriteToStorage++;
                t2.erase(itLRUValue);
                t2_key.remove(*itLRU);

                PRINTV(logfile << "REPLACE (NVM is filled with dirty pages) evicting t2 and flushing back " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
        }
    }

private:

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history clean page LRU list
    key_tracker_type t1_key;
    ///ziqi: Key-to-value lookup clean page in DRAM
    key_to_value_type t1a;
    ///ziqi: Key-to-value lookup clean page in NVRAM
    key_to_value_type t1b;
    ///ziqi: Key access history dirty page LRU list
    key_tracker_type t2_key;
    ///ziqi: Key-to-value lookup dirty page in NVRAM
    key_to_value_type t2;
    ///ziqi: Key access history clean ghost LRU list
    key_tracker_type b1_key;
    ///ziqi: Key-to-value lookup clean ghost pages
    key_to_value_type b1;
    ///ziqi: Key access history dirty ghost LRU list
    key_tracker_type b2_key;
    ///ziqi: Key-to-value lookup dirty ghost pages
    key_to_value_type b2;
};

#endif //end hybrid
