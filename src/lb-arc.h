//
// C++ Interface: large block ARC
//
// Description:
//
//
// Author: Ziqi Fan
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef LBARC_H
#define LBARC_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

///ziqi: in access(), if the value's time stamp is the first one that equal or bigger than a multiple of 30s, then flushing back all the dirty pages in buffer cache.
///Then change the dirty pages status to clean. Log these dirty pages into DiskSim input trace.
///ziqi: in access(), for status is write, add dirty page notation to the status.
///(done) ziqi: in remove(), modify it as lru_ziqi.h. Log the evicted dirty page.

extern int totalPageWriteToStorage;

extern int totalLargeBlockWriteToStorage;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class LBARC : public TestCache<K, V>
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

    LBARC(
        V(*f)(const K & , V),
        size_t c,
        uint32_t pb,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), _pagePerBlock(pb), levelMinusMinus(levelMinus)  {
        assert ( _capacity!=0 );
    }

    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);

        // denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1.size() + t2.size()) <= _capacity);
        assert((t1.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert((t1.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);
        PRINTV(logfile << "Corresponding large block: " << k/_pagePerBlock <<endl;);
        PRINTV(logfile << "Cache status: ** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1 = t1.find(k);
        const typename key_to_value_type::iterator it_t2 = t2.find(k);
        const typename key_to_value_type::iterator it_b1 = b1.find(k);
        const typename key_to_value_type::iterator it_b2 = b2.find(k);

        // LB-ARC Case I: x hit in t1 or t2, then move x to t2
        // update large block accordingly
        if((it_t1 != t1.end()) || (it_t2 != t2.end())) {
            assert(!((it_t1 != t1.end()) && (it_t2 != t2.end())));

            if((it_t1 != t1.end()) && (it_t2 == t2.end())) {
                PRINTV(logfile << "Case I Hit on t1: " << k << endl;);

                // evict hit page from t1
                t1.erase(it_t1);
                typename key_to_value_for_largeblock_type::iterator it_t1_lb = t1_lb.find(k/_pagePerBlock);
                assert(it_t1_lb != t1_lb.end());
                it_t1_lb->second--;
                if(it_t1_lb->second == 0) {
                    t1_lb.erase(k/_pagePerBlock);
                    t1_key.remove(k/_pagePerBlock);
                }

                typename key_to_value_for_largeblock_type::iterator it_t2_lb = t2_lb.find(k/_pagePerBlock);
                /* insert the page belonged block to t2 LRU
                * check new page belonged block exists or not in t2
                * if not exists, insert to new block to MRU position of LRU list
                * else, move the LB to MRU postion of LRU list
                */
                if(it_t2_lb == t2_lb.end()) {
                    t2_key.insert(t2_key.end(), k/_pagePerBlock);
                }
                else {
                    t2_key.remove(k/_pagePerBlock);
                    t2_key.insert(t2_key.end(), k/_pagePerBlock);
                }

                // insert the page to t2 cache
                const V v = _fn(k, value);
                t2.insert(make_pair(k, v));
                PRINTV(logfile << "Insert page to cache: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                // update large block store
                // check new page belonged LB exists or not
                if(it_t2_lb == t2_lb.end()) {
                    t2_lb.insert(make_pair(k/_pagePerBlock, 1));
                    PRINTV(logfile << "Insert large block: " << k/_pagePerBlock <<endl;);
                }
                else {
                    it_t2_lb->second++;
                    PRINTV(logfile << "Enlarge large block: " << k/_pagePerBlock <<endl;);
                }
            }

            if((it_t2 != t2.end()) && (it_t1 == t1.end())) {
                PRINTV(logfile << "Case I Hit on t2: " << k << endl;);

                t2_key.remove(k/_pagePerBlock);
                t2_key.insert(t2_key.end(), k/_pagePerBlock);

                PRINTV(logfile << "Re-arrange t2 block LRU order: " << k/_pagePerBlock <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);

        }

        // LB-ARC Case II: x hit in b1, then enlarge t1 and move x from b1 to t2
        else if(it_b1 != b1.end()) {
            PRINTV(logfile << "Case II Hit on b1: " << k << endl;);

            // delta denotes the step in each ADAPTATION
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

            const V v = _fn(k, value);

            REPLACE(k, v, p);

            b1.erase(it_b1);
            b1_key.remove(k);
            PRINTV(logfile << "Case II evict b1 (just a page not a LB): " << k <<  endl;);

            typename key_to_value_for_largeblock_type::iterator it_t2_lb = t2_lb.find(k/_pagePerBlock);
            /* insert the page belonged block to t2 LRU
            * check new page belonged block exists or not in t2
            * if not exists, insert to new block to MRU position of LRU list
            * else, move the LB to MRU postion of LRU list
            */
            if(it_t2_lb == t2_lb.end()) {
                t2_key.insert(t2_key.end(), k/_pagePerBlock);
            }
            else {
                t2_key.remove(k/_pagePerBlock);
                t2_key.insert(t2_key.end(), k/_pagePerBlock);
            }

            // insert the page to t2 cache
            t2.insert(make_pair(k, v));
            PRINTV(logfile << "Insert page to cache: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

            // update large block store
            // check new page belonged LB exists or not
            if(it_t2_lb == t2_lb.end()) {
                t2_lb.insert(make_pair(k/_pagePerBlock, 1));
                PRINTV(logfile << "Insert large block: " << k/_pagePerBlock <<endl;);
            }
            else {
                it_t2_lb->second++;
                PRINTV(logfile << "Enlarge large block: " << k/_pagePerBlock <<endl;);
            }

            return (status | PAGEMISS);
        }

        // LB-ARC Case III: x hit in b2, then enlarge t2 and move x from b2 to t2
        else if(it_b2 != b2.end()) {
            PRINTV(logfile << "Case III Hit on b2: " << k << endl;);

            // delta denotes the step in each ADAPTATION
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

            const V v = _fn(k, value);

            REPLACE(k, v, p);

            b2.erase(it_b2);
            b2_key.remove(k);
            PRINTV(logfile << "Case II evict b2 (just a page not a LB): " << k <<  endl;);

            typename key_to_value_for_largeblock_type::iterator it_t2_lb = t2_lb.find(k/_pagePerBlock);
            /* insert the page belonged block to t2 LRU
            * check new page belonged block exists or not in t2
            * if not exists, insert to new block to MRU position of LRU list
            * else, move the LB to MRU postion of LRU list
            */
            if(it_t2_lb == t2_lb.end()) {
                t2_key.insert(t2_key.end(), k/_pagePerBlock);
            }
            else {
                t2_key.remove(k/_pagePerBlock);
                t2_key.insert(t2_key.end(), k/_pagePerBlock);
            }

            // insert the page to t2 cache
            t2.insert(make_pair(k, v));
            PRINTV(logfile << "Insert page to cache: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

            // update large block store
            // check new page belonged LB exists or not
            if(it_t2_lb == t2_lb.end()) {
                t2_lb.insert(make_pair(k/_pagePerBlock, 1));
                PRINTV(logfile << "Insert large block: " << k/_pagePerBlock <<endl;);
            }
            else {
                it_t2_lb->second++;
                PRINTV(logfile << "Enlarge large block: " << k/_pagePerBlock <<endl;);
            }

            return (status | PAGEMISS);
        }

        //  LB-ARC Case IV: x is cache miss
        else {
            PRINTV(logfile << "Case IV miss on key: " << k << endl;);

            // Case A
            if((t1.size() + b1.size()) == _capacity) {
                // b1 is not empty, evict LRU of b1
                if(t1.size() < _capacity) {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    PRINTV(logfile << "Case IV evicting b1 " << *itLRU <<  endl;);

                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
                // b1 is empty, evict LRU LB of t1
                else {
                    // select the LRU block of t1
                    int lruBlockNumber = *(t1_key.begin());
                    typename key_to_value_for_largeblock_type::iterator it_eviction = t1_lb.find(lruBlockNumber);
                    assert(it_eviction != t1_lb.end());

                    // evict all pages from the victim block
                    PRINTV(logfile << "evict lru block = " << lruBlockNumber << endl;);
                    int evcitedPageFromBlock = 0;
                    for(int i=0; i<int(_pagePerBlock); i++) {
                        typename key_to_value_type::iterator it_tmp = t1.find(lruBlockNumber*_pagePerBlock+i);
                        if(it_tmp != t1.end()) {
                            t1.erase(it_tmp);
                            evcitedPageFromBlock++;
                        }
                    }
                    assert(evcitedPageFromBlock == it_eviction->second);
                    PRINTV(logfile << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

                    // evict the victim block from CLOCK and large block store
                    t1_key.remove(lruBlockNumber);
                    t1_lb.erase(lruBlockNumber);

                    totalPageWriteToStorage += evcitedPageFromBlock;
                    totalLargeBlockWriteToStorage++;
                }
            }

            // Case B
            else if((t1.size() + b1.size()) < _capacity) {
                if((t1.size() + t2.size() + b1.size() + b2.size()) >= _capacity) {
                    if((t1.size() + t2.size() + b1.size() + b2.size()) == 2 * _capacity) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                    }
                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
            }

            typename key_to_value_for_largeblock_type::iterator it_t1_lb = t1_lb.find(k/_pagePerBlock);
            /* insert the missed page belonged block to t1 LRU
            * check new page belonged block exists or not in t1
            * if not exists, insert to new block to MRU position of LRU list
            * else, move the LB to MRU postion of LRU list
            */
            if(it_t1_lb == t1_lb.end()) {
                t1_key.insert(t1_key.end(), k/_pagePerBlock);
            }
            else {
                t1_key.remove(k/_pagePerBlock);
                t1_key.insert(t1_key.end(), k/_pagePerBlock);
            }

            // insert the page to t1 cache
            const V v = _fn(k, value);
            t1.insert(make_pair(k, v));
            PRINTV(logfile << "Insert page to cache: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

            // update large block store
            // check new page belonged LB exists or not
            if(it_t1_lb == t1_lb.end()) {
                t1_lb.insert(make_pair(k/_pagePerBlock, 1));
                PRINTV(logfile << "Insert large block: " << k/_pagePerBlock <<endl;);
            }
            else {
                it_t1_lb->second++;
                PRINTV(logfile << "Enlarge large block: " << k/_pagePerBlock <<endl;);
            }

            return (status | PAGEMISS);
        }

        return 0;

    } //end operator access

    // LB-ARC subroutine
    void REPLACE(const K &k, const V &v, int p) {
        typename key_to_value_type::iterator it = b2.find(k);

        // evict from t1 side LRU block and insert them to b1
        if((t1.size() > 0) && ((t1.size() > unsigned(p)) || ((it != b2.end()) && (t1.size() == unsigned(p))))) {

            // select the LRU block of t1
            int lruBlockNumber = *(t1_key.begin());
            PRINTV(logfile << "lruBlockNumber = " << lruBlockNumber << endl;);
            typename key_to_value_for_largeblock_type::iterator it_eviction = t1_lb.find(lruBlockNumber);
            assert(it_eviction != t1_lb.end());

            // insert all page from the victim block to b1
            PRINTV(logfile << "insert to b1" << endl;);
            int insertedPageFromBlock = 0;
            for(int i=0; i<int(_pagePerBlock); i++) {
                typename key_to_value_type::iterator it_tmp = t1.find(lruBlockNumber*_pagePerBlock+i);
                if(it_tmp != t1.end()) {
                    b1_key.insert(b1_key.end(), lruBlockNumber*_pagePerBlock+i);
                    const V v_tmp = _fn(lruBlockNumber*_pagePerBlock+i, it_tmp->second);
                    b1.insert(make_pair(lruBlockNumber*_pagePerBlock+i, v_tmp));
                    insertedPageFromBlock++;
                }
            }
            assert(insertedPageFromBlock == it_eviction->second);
            PRINTV(logfile << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

            // evict all pages from the victim block
            PRINTV(logfile << "evict lru block = " << lruBlockNumber << endl;);
            int evcitedPageFromBlock = 0;
            for(int i=0; i<int(_pagePerBlock); i++) {
                typename key_to_value_type::iterator it_tmp = t1.find(lruBlockNumber*_pagePerBlock+i);
                if(it_tmp != t1.end()) {
                    t1.erase(it_tmp);
                    evcitedPageFromBlock++;
                }
            }
            assert(evcitedPageFromBlock == it_eviction->second);
            PRINTV(logfile << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

            // evict the victim block from CLOCK and large block store
            t1_key.remove(lruBlockNumber);
            t1_lb.erase(lruBlockNumber);

            totalPageWriteToStorage += evcitedPageFromBlock;
            totalLargeBlockWriteToStorage++;
        }
        // evict from t2 side
        else {

            // select the LRU block of t2
            int lruBlockNumber = *(t2_key.begin());
            PRINTV(logfile << "lruBlockNumber = " << lruBlockNumber << endl;);
            typename key_to_value_for_largeblock_type::iterator it_eviction = t2_lb.find(lruBlockNumber);
            assert(it_eviction != t2_lb.end());

            // insert all page from the victim block to b2
            PRINTV(logfile << "insert to b2" << endl;);
            int insertedPageFromBlock = 0;
            for(int i=0; i<int(_pagePerBlock); i++) {
                typename key_to_value_type::iterator it_tmp = t2.find(lruBlockNumber*_pagePerBlock+i);
                if(it_tmp != t2.end()) {
                    b2_key.insert(b2_key.end(), lruBlockNumber*_pagePerBlock+i);
                    const V v_tmp = _fn(lruBlockNumber*_pagePerBlock+i, it_tmp->second);
                    b2.insert(make_pair(lruBlockNumber*_pagePerBlock+i, v_tmp));
                    insertedPageFromBlock++;
                }
            }
            assert(insertedPageFromBlock == it_eviction->second);
            PRINTV(logfile << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

            // evict all pages from the victim block
            PRINTV(logfile << "evict lru block = " << lruBlockNumber << endl;);
            int evcitedPageFromBlock = 0;
            for(int i=0; i<int(_pagePerBlock); i++) {
                typename key_to_value_type::iterator it_tmp = t2.find(lruBlockNumber*_pagePerBlock+i);
                if(it_tmp != t2.end()) {
                    t2.erase(it_tmp);
                    evcitedPageFromBlock++;
                }
            }
            assert(evcitedPageFromBlock == it_eviction->second);
            PRINTV(logfile << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);

            // evict the victim block from CLOCK and large block store
            t2_key.remove(lruBlockNumber);
            t2_lb.erase(lruBlockNumber);

            totalPageWriteToStorage += evcitedPageFromBlock;
            totalLargeBlockWriteToStorage++;
        }
    }

private:

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;
    // How many pages in a SSD block
    const uint32_t _pagePerBlock;

    unsigned levelMinusMinus;

    // large block access history
    key_tracker_type t1_key;
    // Key-to-value lookup
    key_to_value_type t1;
    // key to block lookup
    key_to_value_for_largeblock_type t1_lb;
    // large block access history
    key_tracker_type t2_key;
    // Key-to-value lookup
    key_to_value_type t2;
    // key to block lookup
    key_to_value_for_largeblock_type t2_lb;
    // Key access history
    key_tracker_type b1_key;
    // Key-to-value lookup
    key_to_value_type b1;
    // Key access history
    key_tracker_type b2_key;
    // Key-to-value lookup
    key_to_value_type b2;
};

#endif //end LB-ARC
