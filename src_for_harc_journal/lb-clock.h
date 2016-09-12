//
// C++ Interface: Large Block CLOCK
//
// Description:
//
//
// Author: Ziqi Fan, ziqifan16@gmail.com, April 10, 2016
//
//

#ifndef LBCLOCK_H
#define LBCLOCK_H

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

extern int totalLargeBlockWriteToStorage;

template <typename K, typename V>
class LBCLOCK : public TestCache<K, V>
{
public:
    // track large block position in the CLOCK
    typedef list<int> key_tracker_type;
    // store all cached pages
    typedef map<K, V> key_to_value_type;
    /*
     * First is the large block number, calculated by k/_pagePerBlock
     * Second is the number of how many cached pages belong to this large block
     * Thrid is reference bit, 0 is cold and 1 is hot
     * */
    typedef map<int, pair<int, int>> key_to_value_for_largeblock_type;

    // store the clock hand pointing to large block, the value is the large block number
    // when need to evict, clock scans starting from the item with the key stored in clockHand
    int clockHand = 0;
    // for first item in trace, it is used to initiate the clock hand
    bool initClockHand = true;

    LBCLOCK(
        V(*f)(const K & , V),
        size_t c,
        uint32_t pb,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), _pagePerBlock(pb), levelMinusMinus(levelMinus)  {
        assert ( _capacity!=0 );
    }

    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        assert(_key_to_value.size() <= _capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);
        PRINTV(logfile << "Page per block: " << _pagePerBlock << endl;);
        PRINTV(logfile << "cache size: " << _key_to_value.size() << endl;);
        PRINTV(logfile << "tracker size: " << _key_tracker.size() << endl;);
        PRINTV(logfile << "large block meta store size: " << large_block.size() << endl;);

        const typename key_to_value_type::iterator it = _key_to_value.find(k);

        // Case I: cache miss
        if(it == _key_to_value.end()) {
            PRINTV(logfile << "Case I Miss on key: " << k << endl;);

            // if cache is full, start eviction
            if(_key_to_value.size() == _capacity) {
                PRINTV(logfile << "Cache is Full " << _key_to_value.size() << endl;);

                // iterate clock list to find clockHand
                typename key_tracker_type::iterator it_list_clock = _key_tracker.begin();
                for(; it_list_clock != _key_tracker.end(); it_list_clock++) {
                    if(*it_list_clock == clockHand) {
                        PRINTV(logfile << "found clockHand " << clockHand << endl;);
                        break;
                    }
                }
                assert(it_list_clock != _key_tracker.end());
                assert(!_key_tracker.empty());

                /* start to evict the large block
                 * the victim block is among cold blocks and contains the most pages
                 */
                int maxNumber = 0;
                int maxLargeBlock = 0;
                typename key_to_value_for_largeblock_type::iterator it_eviction = large_block.begin();
                for(; it_eviction != large_block.end(); it_eviction++) {
                    if(it_eviction->second.second == 0) {
                        if(it_eviction->second.first > maxNumber) {
                            maxNumber = it_eviction->second.first;
                            maxLargeBlock = it_eviction->first;
                        }
                    }
                }
                if(maxLargeBlock == 0) { // all blocks are hot now
                    maxLargeBlock = large_block.begin()->first;
                    maxNumber = large_block.begin()->second.first;
                    PRINTV(logfile << "All blocks are hot so victim block is the first block in large_block = " << maxLargeBlock << endl;);
                }

                // evict all pages from the victim block
                PRINTV(logfile << "maxLargeBlock = " << maxLargeBlock <<", maxNumber = "<< maxNumber << endl;);
                int evcitedPageFromBlock = 0;
                for(int i=0; i<int(_pagePerBlock); i++) {
                    typename key_to_value_type::iterator it_tmp = _key_to_value.find(maxLargeBlock*_pagePerBlock+i);
                    if(it_tmp != _key_to_value.end()) {
                        _key_to_value.erase(it_tmp);
                        evcitedPageFromBlock++;
                    }
                }
                PRINTV(logfile << "evcitedPageFromBlock = " << evcitedPageFromBlock << endl;);
                assert(evcitedPageFromBlock == maxNumber);

                // clockHand will get evicted, we have to assign the next one in the CLOCK as the new clockHand
                if(clockHand == maxLargeBlock) {
                    if(++it_list_clock == _key_tracker.end()) {
                        clockHand = *(_key_tracker.begin());
                        PRINTV(logfile <<__LINE__ << endl;);
                    }
                    else {
                        clockHand = *it_list_clock;
                        PRINTV(logfile <<__LINE__ << endl;);
                    }
                    // since it_list_clock++ before
                    it_list_clock--;

                    PRINTV(logfile << "original clockHand is evicted, new clockHand = " << clockHand << endl;);
                }

                // evict the victim block from CLOCK and large block meta store
                _key_tracker.remove(maxLargeBlock);
                large_block.erase(maxLargeBlock);

                totalPageWriteToStorage += evcitedPageFromBlock;
                totalLargeBlockWriteToStorage++;
                /* eviction end here
                 */

                // find the item in large block meta store with the key of clockHand
                typename key_to_value_for_largeblock_type::iterator it_clock = large_block.find(clockHand);
                assert(it_clock != large_block.end());

                // iterate clock list to find clockHand
                it_list_clock = _key_tracker.begin();
                for(; it_list_clock != _key_tracker.end(); it_list_clock++) {
                    if(*it_list_clock == clockHand) {
                        PRINTV(logfile << "found clockHand " << clockHand << endl;);
                        break;
                    }
                }
                assert(it_list_clock != _key_tracker.end());
                assert(!_key_tracker.empty());

                // start CLOCK looping to mark cold blocks
                while(true) {
                    /// for a cold page, stop scanning
                    if(it_clock->second.second == 0) {
                        // keep a note what the clockHand should be for next eviciton
                        // clockHand should be the next item in the clock list
                        if(++it_list_clock == _key_tracker.end()) {
                            clockHand = *(_key_tracker.begin());
                        }
                        else {
                            clockHand = *it_list_clock;
                        }
                        // since it_list_clock++ before
                        it_list_clock--;
                        PRINTV(logfile << "new clockhand for next scan is " << clockHand << endl;);

                        break;
                    }
                    // for a hot page, change it to cold
                    else {
                        PRINTV(logfile << "skip a hot block " << it_clock->first << " " << it_clock->second.second<<  endl;);
                        it_clock->second.second = 0;
                        PRINTV(logfile << "change a hot block to a cold block " << " " << it_clock->second.second <<  endl;);
                    }

                    // increae it_list_clock to check next item in clock list
                    it_list_clock++;
                    if(it_list_clock == _key_tracker.end()) {
                        it_list_clock = _key_tracker.begin();
                    }
                    PRINTV(logfile <<" *it_list_clock " << *it_list_clock <<  endl;);
                    it_clock = large_block.find(*it_list_clock);
                }
            }

            // insert page
            // for the very first item in the trace, use it as the clockHand
            if(initClockHand) {
                initClockHand = false;

                // insert the page belonged LB to CLOCK
                _key_tracker.insert(_key_tracker.end(), k/_pagePerBlock);

                // insert the page to cache
                const V v = _fn(k, value);
                _key_to_value.insert(make_pair(k, v));
                PRINTV(logfile << "Insert page to cache: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                // update LB
                // newly inserted LB as cold block
                large_block.insert(make_pair(k/_pagePerBlock, make_pair(1, 0)));
                PRINTV(logfile << "Insert large block: " << k/_pagePerBlock << endl;);

                // init clockHand to first cached item
                clockHand = k/_pagePerBlock;
                PRINTV(logfile << "first clockHand: " << clockHand << endl;);
            }
            // if not the very first item, clockHand is initiated already
            else {
                typename key_to_value_for_largeblock_type::iterator it_lb = large_block.find(k/_pagePerBlock);

                /* insert the page belonged LB to CLOCK
                 * check new page belonged LB exists or not
                 * if not exists, insert to new LB to CLOCK
                 * else, change the LB to hot
                 */
                if(it_lb == large_block.end()) {
                    // find the clockHand from clock list
                    // and insert the new item just in front of the clockHand in clock list
                    typename key_tracker_type::iterator it_list_clock = _key_tracker.begin();
                    for(; it_list_clock != _key_tracker.end(); it_list_clock++) {
                        if(*it_list_clock == clockHand) {
                            PRINTV(logfile << "found clockHand " << clockHand << endl;);
                            break;
                        }
                    }
                    assert(it_list_clock != _key_tracker.end());
                    assert(!_key_tracker.empty());
                    _key_tracker.insert(it_list_clock, k/_pagePerBlock);
                }
                else {
                    it_lb->second.second = 1;
                }

                // insert the page to cache
                const V v = _fn(k, value);
                _key_to_value.insert(make_pair(k, v));
                PRINTV(logfile << "Insert page to clock: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                // update LB
                // check new page belonged LB exists or not
                if(it_lb == large_block.end()) {
                    // not exists
                    large_block.insert(make_pair(k/_pagePerBlock, make_pair(1, 0)));
                    PRINTV(logfile << "Insert large block: " << k/_pagePerBlock << endl;);
                }
                else {
                    // exists
                    it_lb->second.first++;
                }
            }
            return (status | PAGEMISS);
        }
        // Case II: cache hit
        else {
            PRINTV(logfile << "Case II Hit on key: " << k << endl;);

            // update LB change the LB to hot
            typename key_to_value_for_largeblock_type::iterator it_lb = large_block.find(k/_pagePerBlock);
            PRINTV(logfile << "changed the hit block hotness is: " << it_lb->second.second <<endl;);
            it_lb->second.second = 1;
            PRINTV(logfile << "changed the hit block to hot: " << it_lb->second.second <<endl;);

            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            return (status | PAGEHIT | BLKHIT);
        }
    } //end operator access

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;
    // How many pages in a SSD block
    const uint32_t _pagePerBlock;
    // Key access history
    key_tracker_type _key_tracker;
    // Key-to-value lookup
    key_to_value_type _key_to_value;
    // large block meta store
    key_to_value_for_largeblock_type large_block;
    unsigned levelMinusMinus;
};

#endif //end LB-CLOCK
