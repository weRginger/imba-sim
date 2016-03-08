//
// C++ Interface: clock cache policy
//
// Description:
//
//
// Author: Ziqi Fan, ziqifan16@gmail.com, Sep 13, 215
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef CLOCK_H
#define CLOCK_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

//#include <boost/circular_buffer.hpp>

using namespace std;

extern int totalPageWriteToStorage;

extern int writeHitOnDirty;

extern int writeHitOnClean;

///extern int dirtyPageInCache;
// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class CLOCK : public TestCache<K, V>
{
public:
    typedef list<K> key_tracker_type;
    typedef map< K, V> key_to_value_type;

    //store the clock hand, the value is the key of the key_to_value_type
    //when need to evict, clock scans starting from the item with the key stored in clockHand
    uint32_t clockHand = 0;
    //for first item in trace, it is used to initiate the clock hand
    bool initClockHand = true;

    CLOCK(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        assert ( _capacity!=0 );
    }

    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        assert(_key_to_value.size() <= _capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        const typename key_to_value_type::iterator it	= _key_to_value.find(k);

        //Case I: cache miss
        if(it == _key_to_value.end()) {
            PRINTV(logfile << "Case I Miss on key: " << k << endl;);

            //if cache is full, start eviction
            if(_key_to_value.size() == _capacity) {
                PRINTV(logfile << "Cache is Full " << _key_to_value.size() << endl;);

                //iterate clock list to find clockHand
                typename key_tracker_type::iterator it_list_clock = _key_tracker.begin();
                for(; it_list_clock != _key_tracker.end(); it_list_clock++) {
                    if(*it_list_clock == clockHand) {
                        PRINTV(logfile << "found clockHand " << clockHand << endl;);
                        break;
                    }
                }
                assert(it_list_clock != _key_tracker.end());
                assert(!_key_tracker.empty());

                //find the item in clock map with the key of clockHand
                typename key_to_value_type::iterator it_clock = _key_to_value.find(*it_list_clock);
                //start a loop of checking whether the item in clock map is cold or not
                //in the order of clock list
                while(true) {
                    ///for a cold page, evict
                    if(it_clock->second.getFlags() & COLD) {
                        //keep a note what the clockHand should be for next eviciton
                        //clockHand should be the next item in the clock list
                        if(++it_list_clock == _key_tracker.end()) {
                            clockHand = *(_key_tracker.begin());
                            PRINTV(logfile <<__LINE__ << "clockHand " << clockHand << endl;);
                        }
                        else {
                            clockHand = *it_list_clock;
                            PRINTV(logfile <<__LINE__ << "clockHand " << clockHand << endl;);
                        }
                        //since it_list_clock++ before
                        it_list_clock--;

                        //start of eviction
                        PRINTV(logfile << "evict a cold page " << it_clock->first << endl;);
                        _key_to_value.erase(it_clock);
                        _key_tracker.remove(*it_list_clock);
                        PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                        //eviction ended
                        break;
                    }
                    ///for a hot page, change it to cold
                    else {
                        PRINTV(logfile << "skip a hot page " << it_clock->first << " " << bitset<13>(it_clock->second.getFlags())<<  endl;);
                        it_clock->second.updateFlags(it_clock->second.getFlags() | COLD);
                        PRINTV(logfile << "change a hot page to a cold page " << " " << bitset<13>(it_clock->second.getFlags()) <<  endl;);
                    }

                    //increae it_list_clock to check next item in clock list
                    it_list_clock++;
                    if(it_list_clock == _key_tracker.end()) {
                        it_list_clock = _key_tracker.begin();
                    }
                    PRINTV(logfile <<__LINE__ <<" *it_list_clock " << *it_list_clock <<  endl;);
                    it_clock = _key_to_value.find(*it_list_clock);
                }
            }

            ///insert page
            //for the very first item in the trace, use it as the clockHand
            if(initClockHand) {
                initClockHand = false;

                _key_tracker.insert(_key_tracker.end(), k);
                //set the page to be cold before insertion
                value.updateFlags(value.getFlags() | COLD);
                const V v = _fn(k, value);
                _key_to_value.insert(make_pair(k, v));
                PRINTV(logfile << "Insert page to clock: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                //init clockHand to first cached item
                typename key_tracker_type::iterator it_list_clock = _key_tracker.begin();
                clockHand = *it_list_clock;
                PRINTV(logfile <<__LINE__ << "clockHand " << clockHand << endl;);
            }
            //if not the very first item, clockHand is initiated already
            else {
                //find the clockHand from clock list
                //and insert the new item just in front of the clockHand in clock list
                typename key_tracker_type::iterator it_list_clock = _key_tracker.begin();
                for(; it_list_clock != _key_tracker.end(); it_list_clock++) {
                    PRINTV(logfile << "clockHand " << clockHand << " *it_list_clock " << *it_list_clock << endl;);
                    if(*it_list_clock == clockHand) {
                        PRINTV(logfile << "found clockHand " << clockHand << endl;);
                        break;
                    }
                }
                assert(it_list_clock != _key_tracker.end());
                assert(!_key_tracker.empty());
                _key_tracker.insert(it_list_clock, k);
                //set the page to be cold before insertion
                value.updateFlags(value.getFlags() | COLD);
                const V v = _fn(k, value);
                _key_to_value.insert(make_pair(k, v));
                PRINTV(logfile << "Insert page to clock: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            }

            return (status | PAGEMISS);
        }
        ///Case II: cache hit
        else {
            PRINTV(logfile << "Case II Hit on key: " << k << endl;);
            PRINTV(logfile << "before flags: " << bitset<13>(it->second.getFlags()) << endl;);
            //if the current page is cold, change it to hot
            if(it->second.getFlags() & COLD) {
                it->second.updateFlags(it->second.getFlags() ^ COLD);
            }
            PRINTV(logfile << "after flags: " << bitset<13>(it->second.getFlags()) << endl;);

            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            return (status | PAGEHIT | BLKHIT);
        }
    } //end operator access

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;
    // Key access history
    key_tracker_type _key_tracker;
    // Key-to-value lookup
    key_to_value_type _key_to_value;
    unsigned levelMinusMinus;
};

#endif //end clock
