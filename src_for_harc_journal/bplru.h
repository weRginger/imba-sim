//
// C++ Interface: BPLRU - block padding LRU
//
// Description:
//
//
// Author: Ziqi Fan, ziqifan16@gmail.com, April 10, 2016
//
//

#ifndef BPLRU_H
#define BPLRU_H

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
class BPLRU : public TestCache<K, V>
{
public:
    // track large block position in LRU
    typedef list<int> key_tracker_type;
    // store all cached pages
    typedef map<K, V> key_to_value_type;
    /*
     * First is the large block number, calculated by k/_pagePerBlock
     * Second is the number of how many cached pages belong to this large block
     * */
    typedef map<int, int> key_to_value_for_largeblock_type;

    BPLRU(
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
        PRINTV(logfile << "large block store size: " << large_block.size() << endl;);

        const typename key_to_value_type::iterator it = _key_to_value.find(k);

        // Case I: cache miss
        if(it == _key_to_value.end()) {
            PRINTV(logfile << "Case I Miss on key: " << k << endl;);

            // if cache is full, start eviction
            if(_key_to_value.size() == _capacity) {
                PRINTV(logfile << "Cache is Full " << _key_to_value.size() << endl;);

                // select the LRU block
                int lruBlockNumber = *(_key_tracker.begin());
                typename key_to_value_for_largeblock_type::iterator it_eviction = large_block.find(lruBlockNumber);

                // evict all pages from the victim block
                PRINTV(logfile << "evict lru block = " << lruBlockNumber << endl;);
                int evcitedPageFromBlock = 0;
                for(int i=0; i<int(_pagePerBlock); i++) {
                    typename key_to_value_type::iterator it_tmp = _key_to_value.find( lruBlockNumber*_pagePerBlock+i);
                    if(it_tmp != _key_to_value.end()) {
                        _key_to_value.erase(it_tmp);
                        evcitedPageFromBlock++;
                    }
                }
                assert(evcitedPageFromBlock == it_eviction->second);

                // evict the victim block from CLOCK and large block store
                _key_tracker.remove(lruBlockNumber);
                large_block.erase(lruBlockNumber);

                totalPageWriteToStorage += evcitedPageFromBlock;
                totalLargeBlockWriteToStorage++;
                /* eviction end here */
            }

            // insert page

            typename key_to_value_for_largeblock_type::iterator it_lb = large_block.find(k/_pagePerBlock);

            /* insert the page belonged block to LRU
             * check new page belonged block exists or not
             * if not exists, insert to new block to MRU position of LRU list
             * else, move the LB to MRU postion of LRU list
             */
            if(it_lb == large_block.end()) {
                _key_tracker.insert(_key_tracker.end(), k/_pagePerBlock);
            }
            else {
                _key_tracker.remove(k/_pagePerBlock);
                _key_tracker.insert(_key_tracker.end(), k/_pagePerBlock);
            }

            // insert the page to cache
            const V v = _fn(k, value);
            _key_to_value.insert(make_pair(k, v));
            PRINTV(logfile << "Insert page to cache: " << k << endl;);
            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

            // update large block store
            // check new page belonged LB exists or not
            if(it_lb == large_block.end()) {
                large_block.insert(make_pair(k/_pagePerBlock, 1));
                PRINTV(logfile << "Insert large block: " << k/_pagePerBlock << endl;);
            }
            else {
                it_lb->second++;
            }
            return (status | PAGEMISS);
        }
        // Case II: cache hit
        else {
            PRINTV(logfile << "Case II Hit on key: " << k << endl;);

            // move the block to MRU postion in LRU list
            _key_tracker.remove(k/_pagePerBlock);
            _key_tracker.insert(_key_tracker.end(), k/_pagePerBlock);

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
    // large block store
    key_to_value_for_largeblock_type large_block;
    unsigned levelMinusMinus;
};

#endif //end BPLRU
