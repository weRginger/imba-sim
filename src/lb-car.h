//
// C++ Interface: Large Block CLOCK with Adaptive Replacement
//
// Description:
//
//
// Author: ARH,,, <arh@aspire-one>, (C) 2011
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef LBCAR_H
#define LBCAR_H

#include <map>
#include <list>
#include <algorithm>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

extern int totalPageWriteToStorage;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class LBCAR : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map< K, V> key_to_value_type;
    // store the clock hand, the value is the key of the key_to_value_type
    // when need to evict, clock scans starting from the item with the key stored in clockHand
    uint32_t clockHand_t1 = 0;
    uint32_t clockHand_t2 = 0;

    LBCAR(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
    }


    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        // ziqi: p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1.size() + t2.size()) <= _capacity);
        assert((t1.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert((t1.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1 = t1.find(k);
        const typename key_to_value_type::iterator it_t2 = t2.find(k);
        const typename key_to_value_type::iterator it_b1 = b1.find(k);
        const typename key_to_value_type::iterator it_b2 = b2.find(k);

        // CAR Case I: x hit in t1 or t2, set the page to hot
        if((it_t1 != t1.end()) || (it_t2 != t2.end())) {
            assert(!((it_t1 != t1.end()) && (it_t2 != t2.end())));
            if((it_t1 != t1.end()) && (it_t2 == t2.end())) {
                PRINTV(logfile << "Case I Hit on t1 " << k << endl;);

                PRINTV(logfile << "before flags: " << bitset<13>(it_t1->second.getFlags()) << endl;);
                //if the current page is cold, change it to hot
                if(it_t1->second.getFlags() & COLD) {
                    it_t1->second.updateFlags(it_t1->second.getFlags() ^ COLD);
                }
                PRINTV(logfile << "after flags: " << bitset<13>(it_t1->second.getFlags()) << endl;);

                PRINTV(logfile << "Case I: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }

            if((it_t2 != t2.end()) && (it_t1 == t1.end())) {
                PRINTV(logfile << "Case I Hit on t2 " << k << endl;);

                PRINTV(logfile << "before flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                //if the current page is cold, change it to hot
                if(it_t2->second.getFlags() & COLD) {
                    it_t2->second.updateFlags(it_t2->second.getFlags() ^ COLD);
                }
                PRINTV(logfile << "after flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);

                PRINTV(logfile << "Case I: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);
        }
        // CAR: Cache miss
        else {
            // cache is full
            if( (t1.size() + t2.size()) == _capacity) {
                const V v = _fn(k, value);
                REPLACE(k, v, p);

                // evict LRU page from b1
                if( (it_b1 == b1.end() && it_b2 == b2.end()) && (t1.size() + b1.size()) == _capacity)
                {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    PRINTV(logfile << "Evicting LRU page from b1 " << *itLRU <<  endl;);
                }
                // evict LRU page from b2
                else if( (t1.size() + t2.size() + b1.size() + b2.size()) == 2*_capacity && (it_b1 == b1.end() && it_b2 == b2.end()) ) {
                    typename key_tracker_type::iterator itLRU = b2_key.begin();
                    assert(itLRU != b2_key.end());
                    typename key_to_value_type::iterator it = b2.find(*itLRU);
                    b2.erase(it);
                    b2_key.remove(*itLRU);
                    PRINTV(logfile << "Evicting LRU page from b2 " << *itLRU <<  endl;);
                }
            }

            // b1 and b2 miss also, insert x at the tail of t1
            if( it_b1 == b1.end() && it_b2 == b2.end() ) {
                // insert page
                // for the very first item in the trace, use it as the clockHand
                if(t1.size() == 0) {
                    t1_key.insert(t1_key.end(), k);
                    // set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t1.insert(make_pair(k, v));
                    PRINTV(logfile << "First time insert page to clock t1: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    // init clockHand to first cached item
                    typename key_tracker_type::iterator it_list_clock = t1_key.begin();
                    clockHand_t1 = *it_list_clock;
                    PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
                }
                // if not the very first item, clockHand is initiated already
                else {
                    // find the clockHand from clock t1
                    // and insert the new item just in front of the clockHand in clock t1
                    typename key_tracker_type::iterator it_list_clock = t1_key.begin();
                    for(; it_list_clock != t1_key.end(); it_list_clock++) {
                        PRINTV(logfile << "clockHand_t1 " << clockHand_t1 << " *it_list_clock " << *it_list_clock << endl;);
                        if(*it_list_clock == clockHand_t1) {
                            PRINTV(logfile << "found clockHand_t1 " << clockHand_t1 << endl;);
                            break;
                        }
                    }
                    assert(it_list_clock != t1_key.end());
                    assert(!t1_key.empty());
                    t1_key.insert(it_list_clock, k);
                    // set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t1.insert(make_pair(k, v));
                    PRINTV(logfile << "Insert page to clock t1: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            // b1 hit
            else if ( it_b1 != b1.end() ) {
                assert(it_b2 == b2.end());

                // adaptation
                int delta;
                if(b1.size() >= b2.size()) {
                    delta = 1;
                }
                else {
                    delta = int(b2.size()/b1.size());
                }
                PRINTV(logfile << "ADAPTATION: p increases from " << p << " to ";);
                if((p+delta) > int(_capacity)) {
                    p = _capacity;
                }
                else {
                    p = p+delta;
                }
                PRINTV(logfile << p << endl;);

                // delete x from b1
                b1.erase(it_b1);
                b1_key.remove(k);

                // insert x to tail of clock t2

                // for the very first item in the trace, use it as the clockHand
                if(t2.size() ==0) {
                    t2_key.insert(t2_key.end(), k);
                    //set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t2.insert(make_pair(k, v));
                    PRINTV(logfile << "Insert page to clock: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    //init clockHand to first cached item
                    typename key_tracker_type::iterator it_list_clock = t2_key.begin();
                    clockHand_t2 = *it_list_clock;
                    PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                }
                //if not the very first item, clockHand is initiated already
                else {
                    // find the clockHand from clock list
                    // and insert the new item just in front of the clockHand in clock list
                    typename key_tracker_type::iterator it_list_clock = t2_key.begin();
                    for(; it_list_clock != t2_key.end(); it_list_clock++) {
                        PRINTV(logfile << "clockHand_t2 " << clockHand_t2 << " *it_list_clock " << *it_list_clock << endl;);
                        if(*it_list_clock == clockHand_t2) {
                            PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
                            break;
                        }
                    }
                    assert(it_list_clock != t2_key.end());
                    assert(!t2_key.empty());
                    t2_key.insert(it_list_clock, k);
                    // set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t2.insert(make_pair(k, v));
                    PRINTV(logfile << "Insert page to clock t2: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            // b2 hit
            else {
                assert(it_b2 != b2.end());
                assert(it_b1 == b1.end());

                // adaptation
                int delta;
                if(b2.size() >= b1.size()) {
                    delta = 1;
                }
                else {
                    delta = int(b1.size()/b2.size());
                }
                PRINTV(logfile << "ADAPTATION: p decreases from " << p << " to ";);
                if((p-delta) > 0) {
                    p = p-delta;
                }
                else {
                    p = 0;
                }
                PRINTV(logfile << p << endl;);

                // delete x from b2
                b2.erase(it_b2);
                b2_key.remove(k);

                // insert x to tail of clock t2

                // for the very first item in the trace, use it as the clockHand
                if(t2.size() == 0) {
                    t2_key.insert(t2_key.end(), k);
                    //set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t2.insert(make_pair(k, v));
                    PRINTV(logfile << "Insert page to clock: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    //init clockHand to first cached item
                    typename key_tracker_type::iterator it_list_clock = t2_key.begin();
                    clockHand_t2 = *it_list_clock;
                    PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                }
                // if not the very first item, clockHand is initiated already
                else {
                    // find the clockHand from clock list
                    // and insert the new item just in front of the clockHand in clock list
                    typename key_tracker_type::iterator it_list_clock = t2_key.begin();
                    for(; it_list_clock != t2_key.end(); it_list_clock++) {
                        PRINTV(logfile << "clockHand_t2 " << clockHand_t2 << " *it_list_clock " << *it_list_clock << endl;);
                        if(*it_list_clock == clockHand_t2) {
                            PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
                            break;
                        }
                    }
                    assert(it_list_clock != t2_key.end());
                    assert(!t2_key.empty());
                    t2_key.insert(it_list_clock, k);
                    // set the page to be cold before insertion
                    value.updateFlags(value.getFlags() | COLD);
                    const V v = _fn(k, value);
                    t2.insert(make_pair(k, v));
                    PRINTV(logfile << "Insert page to clock t2: " << k << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEMISS);
        }
        return 0;
    }

    // ziqi: CAR subroutine
    void REPLACE(const K &k, const V &v, int p) {
        PRINTV(logfile << "Inside REPLACE with k " << k << endl;);
        bool found = false;

        while (found == false) {
            // delete from t1
            if( t1.size() >= (unsigned int)max(1,p) ) {
                // iterate clock list to find clockHand_t1
                typename key_tracker_type::iterator it_clock_t1 = t1_key.begin();
                for(; it_clock_t1 != t1_key.end(); it_clock_t1++) {
                    if(*it_clock_t1 == clockHand_t1) {
                        PRINTV(logfile << "found clockHand_t1 " << clockHand_t1 << endl;);
                        break;
                    }
                }
                assert(it_clock_t1 != t1_key.end());
                assert(!t1_key.empty());

                typename key_to_value_type::iterator it_clock = t1.find(*it_clock_t1);

                // the current page is a cold page
                if(it_clock->second.getFlags() & COLD) {
                    found = true;

                    // keep a note what the clockHand_t1 should be for next eviciton
                    // clockHand_t1 should be the next item in the clock list
                    if(++it_clock_t1 == t1_key.end()) {
                        clockHand_t1 = *(t1_key.begin());
                        PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
                    }
                    else {
                        clockHand_t1 = *it_clock_t1;
                        PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
                    }
                    // since it_clock_t1++ before
                    it_clock_t1--;

                    // insert the page to MRU of b1
                    typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *it_clock_t1);
                    const V v_tmp = _fn(*it_clock_t1, it_clock->second);
                    b1.insert(make_pair(*it_clock_t1, v_tmp));
                    PRINTV(logfile << "REPLACE insert key to MRU of b1: " << *it_clock_t1 << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    // start of eviction
                    PRINTV(logfile << "evict a cold page from t1 " << it_clock->first << endl;);
                    t1.erase(it_clock);
                    t1_key.remove(*it_clock_t1);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                // the current page is a hot page
                else {
                    // keep a note what the clockHand_t1 should be for next eviciton
                    // clockHand_t1 should be the next item in the clock list
                    if(++it_clock_t1 == t1_key.end()) {
                        clockHand_t1 = *(t1_key.begin());
                        PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
                    }
                    else {
                        clockHand_t1 = *it_clock_t1;
                        PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
                    }
                    // since it_clock_t1++ before
                    it_clock_t1--;

                    // insert the current page to tail of clock t2
                    // for the very first item in the trace, use it as the clockHand
                    if(t2.size() == 0) {
                        t2_key.insert(t2_key.end(), *it_clock_t1);
                        // set the page to be cold before insertion
                        it_clock->second.updateFlags(it_clock->second.getFlags() | COLD);
                        const V v = _fn(*it_clock_t1, it_clock->second);
                        t2.insert(make_pair(*it_clock_t1, v));
                        PRINTV(logfile << "Insert page to clock t2: " << *it_clock_t1 << endl;);
                        PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        // init clockHand to first cached item
                        typename key_tracker_type::iterator it_clock_t1 = t2_key.begin();
                        clockHand_t2 = *it_clock_t1;
                        PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                    }
                    // if not the very first item, clockHand is initiated already
                    else {
                        // find the clockHand from clock list
                        // and insert the new item just in front of the clockHand in clock list
                        typename key_tracker_type::iterator it_clock_t2 = t2_key.begin();
                        for(; it_clock_t2 != t2_key.end(); it_clock_t2++) {
                            PRINTV(logfile << "clockHand_t2 " << clockHand_t2 << " *it_clock_t2 " << *it_clock_t2 << endl;);
                            if(*it_clock_t2 == clockHand_t2) {
                                PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
                                break;
                            }
                        }
                        assert(it_clock_t2 != t2_key.end());
                        assert(!t2_key.empty());
                        // insert the key from t1 to the clockhand of t2
                        t2_key.insert(it_clock_t2, *it_clock_t1);
                        // set the page to be cold before insertion
                        it_clock->second.updateFlags(it_clock->second.getFlags() | COLD);
                        const V v = _fn(*it_clock_t1, it_clock->second);
                        t2.insert(make_pair(*it_clock_t1, v));
                        PRINTV(logfile << "Insert page to clock t2: " << *it_clock_t1 << endl;);
                        PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }

                    // start of eviction
                    PRINTV(logfile << "evict a cold page " << it_clock->first << endl;);
                    t1.erase(it_clock);
                    t1_key.remove(it_clock->first);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            // t1 size < p, evict from t2 side
            else {
                // iterate clock list to find clockHand_t2
                typename key_tracker_type::iterator it_list_clock = t2_key.begin();
                for(; it_list_clock != t2_key.end(); it_list_clock++) {
                    if(*it_list_clock == clockHand_t2) {
                        PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
                        break;
                    }
                }
                assert(it_list_clock != t2_key.end());
                assert(!t2_key.empty());

                typename key_to_value_type::iterator it_clock = t2.find(*it_list_clock);

                // the current page is a cold page
                if(it_clock->second.getFlags() & COLD) {
                    found = true;

                    // keep a note what the clockHand_t2 should be for next eviciton
                    // clockHand_t1 should be the next item in the clock list
                    if(++it_list_clock == t2_key.end()) {
                        clockHand_t2 = *(t2_key.begin());
                        PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                    }
                    else {
                        clockHand_t2 = *it_list_clock;
                        PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                    }
                    // since it_list_clock++ before
                    it_list_clock--;

                    // insert the page to MRU of b2
                    typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *it_list_clock);
                    const V v_tmp = _fn(*it_list_clock, it_clock->second);
                    b2.insert(make_pair(*it_list_clock, v_tmp));
                    PRINTV(logfile << "REPLACE insert key to MRU of b2: " << *it_list_clock << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    // start of eviction
                    PRINTV(logfile << "evict a cold page from t2 " << it_clock->first << endl;);
                    t2.erase(it_clock);
                    t2_key.remove(*it_list_clock);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                // the current page is a hot page
                else {
                    // mark the page as cold
                    it_clock->second.updateFlags(it_clock->second.getFlags() | COLD);
                    PRINTV(logfile << "Chage page of clock t2 from hot to cold: " << *it_list_clock << endl;);
                    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    // keep a note what the clockHand_t2 should be for next eviciton
                    // clockHand_t2 should be the next item in the clock list
                    if(++it_list_clock == t2_key.end()) {
                        clockHand_t2 = *(t2_key.begin());
                        PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                    }
                    else {
                        clockHand_t2 = *it_list_clock;
                        PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
                    }
                    // since it_list_clock++ before
                    it_list_clock--;
                }
            }
        }
    }

private:

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history
    key_tracker_type t1_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type t1;
    ///ziqi: Key access history
    key_tracker_type t2_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type t2;
    ///ziqi: Key access history
    key_tracker_type b1_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type b1;
    ///ziqi: Key access history
    key_tracker_type b2_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type b2;
};

#endif //end lru_stl
